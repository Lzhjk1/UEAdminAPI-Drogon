#include "DeltaBuilder.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <openssl/evp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

namespace fs = std::filesystem;

struct FileEntry
{
    std::string path;      // 安装相对路径，反斜杠
    int64_t size = 0;
    std::string sha256;
};

struct JobResult
{
    enum Status { Done = 2, Failed = 3, Skipped = 4 };
};

// ---------- 配置 ----------

bool AutoEnabled()
{
    const auto &cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("Installer") && cfg["Installer"].isObject())
    {
        const auto &node = cfg["Installer"]["DeltaAuto"];
        if (node.isBool())
        {
            return node.asBool();
        }
    }
    return true;
}

std::string ConfigString(const char *key, const std::string &fallback)
{
    const auto &cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("Installer") && cfg["Installer"].isObject())
    {
        const auto &node = cfg["Installer"][key];
        if (node.isString() && !node.asString().empty())
        {
            return node.asString();
        }
    }
    return fallback;
}

std::string StoreDir()
{
    return ConfigString("StoreDir", "./installers");
}

std::string AssetsDir()
{
    const std::string configured = ConfigString("DeltaAssetsDir", "");
    if (!configured.empty())
    {
        return configured;
    }
    std::error_code ec;
    if (fs::exists("./assets/delta/AutoPDMS-project-setup.nsi", ec))
    {
        return "./assets/delta";
    }
    return "/opt/ueadmin/assets";
}

std::string SevenZipPath() { return ConfigString("SevenZipPath", "7z"); }
std::string MakensisPath() { return ConfigString("MakensisPath", "makensis"); }

std::vector<std::string> ForceGroups()
{
    const std::string configured = ConfigString("DeltaForceGroups",
        "project\\UHS\\UHS000,PHSPrjDB,AI\\MCP\\python");
    std::vector<std::string> groups;
    std::stringstream ss(configured);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        if (!item.empty())
        {
            groups.push_back(item);
        }
    }
    return groups;
}

fs::path DeltaTempBase()
{
    std::string tempBase = ConfigString("DeltaTempDir", "");
    if (tempBase.empty())
    {
        if (const char *env = std::getenv("UEADMIN_DELTA_TEMP_DIR"))
        {
            tempBase = env;
        }
    }
    return tempBase.empty() ? fs::temp_directory_path() : fs::path(tempBase);
}

// 清理崩溃/重启遗留的解包目录（每份约 8GB，只清 24 小时前的）
void CleanupStaleWorkDirs()
{
    std::error_code ec;
    const fs::path base = DeltaTempBase();
    if (!fs::is_directory(base, ec))
    {
        return;
    }
    const auto now = fs::file_time_type::clock::now();
    for (auto it = fs::directory_iterator(base, ec); it != fs::directory_iterator();
         it.increment(ec))
    {
        if (ec)
        {
            ec.clear();
            continue;
        }
        const std::string name = it->path().filename().string();
        if (name.rfind("ueap-delta-", 0) != 0)
        {
            continue;
        }
        std::error_code timeEc;
        const auto written = fs::last_write_time(it->path(), timeEc);
        if (timeEc)
        {
            continue;
        }
        if (now - written > std::chrono::hours(24))
        {
            std::error_code rmEc;
            fs::remove_all(it->path(), rmEc);
            LOG_INFO << "清理遗留差量解包目录: " << it->path().string();
        }
    }
}

// ---------- 通用工具 ----------

std::string Quote(const std::string &text)
{
#ifdef _WIN32
    return "\"" + text + "\"";
#else
    std::string out = "'";
    for (char ch : text)
    {
        if (ch == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += ch;
        }
    }
    out += "'";
    return out;
#endif
}

std::string NicePrefix()
{
#ifdef _WIN32
    return std::string();
#else
    return "nice -n 10 ";   // 解包/编译不要和 API 抢满 CPU
#endif
}

int RunCommand(const std::string &command, std::string &output)
{
#ifdef _WIN32
    FILE *pipe = _popen(command.c_str(), "r");
#else
    FILE *pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr)
    {
        output = "启动进程失败";
        return -1;
    }
    char buffer[4096];
    size_t total = 0;
    while (true)
    {
        const size_t readBytes = fread(buffer, 1, sizeof(buffer), pipe);
        if (readBytes == 0)
        {
            break;
        }
        total += readBytes;
        if (output.size() < 32 * 1024)
        {
            output.append(buffer, readBytes);
        }
    }
    const int rc = pclose(pipe);
#ifdef _WIN32
    return rc;
#else
    return (WIFEXITED(rc) ? WEXITSTATUS(rc) : -1);
#endif
}

std::string Sha256FileHex(const fs::path &path, int64_t &outSize)
{
    std::ifstream in(path, std::ios::binary);
    outSize = 0;
    if (!in)
    {
        return std::string();
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == nullptr)
    {
        return std::string();
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return std::string();
    }
    std::vector<char> buffer(1024 * 1024);
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readBytes = in.gcount();
        if (readBytes > 0)
        {
            EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(readBytes));
            outSize += readBytes;
        }
    }
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned int digestLen = 0;
    const bool ok = EVP_DigestFinal_ex(ctx, digest, &digestLen) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok)
    {
        return std::string();
    }
    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve(digestLen * 2);
    for (unsigned int i = 0; i < digestLen; ++i)
    {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string ToBackslash(std::string text)
{
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

// NSIS 字符串转义：$ -> $$，路径分隔符转反斜杠
std::string NsisText(std::string text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text)
    {
        if (ch == '$')
        {
            out += "$$";
        }
        else if (ch == '/')
        {
            out += '\\';
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

std::string ParentDir(const std::string &rel)
{
    const auto pos = rel.find_last_of('\\');
    return (pos == std::string::npos) ? std::string() : rel.substr(0, pos);
}

std::string LeafName(const std::string &rel)
{
    const auto pos = rel.find_last_of('\\');
    return (pos == std::string::npos) ? rel : rel.substr(pos + 1);
}

std::string NowText()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32] = {0};
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

// ---------- 清单 ----------

bool CollectManifest(const fs::path &root, std::vector<FileEntry> &out, std::string &err)
{
    std::error_code ec;
    if (!fs::is_directory(root, ec))
    {
        err = "安装树目录不存在: " + root.string();
        return false;
    }
    out.clear();
    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec)
        {
            ec.clear();
            continue;
        }
        std::string rel = ToBackslash(fs::relative(it->path(), root, ec).generic_string());
        if (ec)
        {
            ec.clear();
            continue;
        }
        if (rel.rfind("$PLUGINSDIR\\", 0) == 0 || rel == "$PLUGINSDIR")
        {
            continue;
        }
        if (rel == "uninst.exe")
        {
            continue;
        }
        const std::string lowerLeaf = Lower(LeafName(rel));
        if (lowerLeaf.size() >= 4 &&
            (lowerLeaf.compare(lowerLeaf.size() - 4, 4, ".ldb") == 0 ||
             lowerLeaf.compare(lowerLeaf.size() - 4, 4, ".dwl") == 0))
        {
            continue;
        }
        if (lowerLeaf.find(".bak") != std::string::npos)
        {
            continue;
        }
        int64_t size = 0;
        const std::string sha = Sha256FileHex(it->path(), size);
        if (sha.empty())
        {
            err = "计算文件 sha256 失败: " + it->path().string();
            return false;
        }
        out.push_back(FileEntry{rel, size, sha});
    }
    std::sort(out.begin(), out.end(), [](const FileEntry &lhs, const FileEntry &rhs) {
        return Lower(lhs.path) < Lower(rhs.path);
    });
    return true;
}

bool InForceGroup(const std::string &rel, const std::vector<std::string> &groups)
{
    const std::string key = Lower(rel);
    for (const auto &group : groups)
    {
        const std::string prefix = Lower(group);
        if (key == prefix || key.rfind(prefix + "\\", 0) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<FileEntry> PickBaseFiles(const std::vector<FileEntry> &base)
{
    static const char *kCandidates[] = {
        "DllLogin10.dll", "APPDMSLogin10.exe", "APAdminUI10.exe", "APAIKernel10.dll",
        "ThirdPartyLoginReturn10.exe", "APHSCommand10.arx", "APMenu10.arx",
        "APHSCalBridge10.dll", "APDomainFrame10.dll", "APDataBaseDriver10.dll"};
    std::vector<FileEntry> picked;
    for (const auto &entry : base)
    {
        const std::string leaf = LeafName(entry.path);
        bool hit = false;
        const std::string lowerLeaf = Lower(leaf);
        for (const char *candidate : kCandidates)
        {
            if (lowerLeaf == Lower(candidate))
            {
                hit = true;
                break;
            }
        }
        if (hit)
        {
            picked.push_back(entry);
            if (picked.size() >= 20)
            {
                break;
            }
        }
    }
    return picked;
}

// ---------- 差值包 NSH ----------

std::string GenerateDeltaNsh(const std::string &fromVersion,
                             const std::string &toVersion,
                             const fs::path &targetRoot,
                             const std::vector<FileEntry> &changed,
                             const std::vector<std::string> &deletes,
                             const std::map<std::string, FileEntry> &baseMap)
{
    std::ostringstream nsh;
    nsh << "; 由服务端 DeltaBuilder 自动生成：" << fromVersion << " -> " << toVersion << "\n";
    nsh << "; 备份策略：覆盖前把被替换/删除的文件复制到 $INSTDIR\\update_backup\\"
        << fromVersion << "\n\n";

    nsh << "!macro DELTA_ABORT reason\n"
           "  StrCpy $R9 \"${reason}\"\n"
           "  Call DeltaWriteFallback\n"
           "  SetErrorLevel 3\n"
           "  Quit\n"
           "!macroend\n\n";

    nsh << "!macro DELTA_TRIM_TAIL var\n"
           "  StrCpy $4 \"\"\n"
           "  StrLen $5 ${var}\n"
           "  StrCpy $6 0\n"
           "  ${Do}\n"
           "    ${If} $6 >= $5\n"
           "      ${ExitDo}\n"
           "    ${EndIf}\n"
           "    StrCpy $3 ${var} 1 $6\n"
           "    ${If} $3 != \" \"\n"
           "    ${AndIf} $3 != \"$\\r\"\n"
           "    ${AndIf} $3 != \"$\\n\"\n"
           "      StrCpy $4 $4$3\n"
           "    ${EndIf}\n"
           "    IntOp $6 $6 + 1\n"
           "  ${Loop}\n"
           "  StrCpy ${var} $4\n"
           "!macroend\n\n";

    nsh << "  SetOutPath \"$INSTDIR\"\n";
    nsh << "  SetOverwrite on\n";
    nsh << "  DetailPrint \"开始增量更新：" << fromVersion << " -> " << toVersion << "\"\n";
    nsh << "  Delete \"$INSTDIR\\AutoCAD-2007-WIN32\\APHSCommand10d.arx\"\n";
    nsh << "  Delete \"$INSTDIR\\AutoCAD-2007-WIN32\\APHSCalBridge10d.dll\"\n";
    nsh << "  Delete \"$INSTDIR\\AutoCAD-2007-WIN32\\APPipeCommand10d.arx\"\n";
    nsh << "  Delete \"$INSTDIR\\AutoCAD-2007-WIN32\\APMenu10d.arx\"\n";
    nsh << "  Delete \"$INSTDIR\\CPP-2005-Win32\\APHSCalBridged.dll\"\n";
    nsh << "  Delete \"$INSTDIR\\CPP-2005-Win32\\APHSCalBridge10d.dll\"\n";
    nsh << "  Delete \"$INSTDIR\\CPP-2005-Win32\\APHSOrgUI10d.dll\"\n\n";

    nsh << "  ; 覆盖前备份（只保留最近一份）\n";
    nsh << "  RMDir /r \"$INSTDIR\\update_backup\"\n";
    std::vector<std::string> backupPaths;
    for (const auto &entry : changed)
    {
        if (baseMap.count(Lower(entry.path)) > 0)
        {
            backupPaths.push_back(entry.path);
        }
    }
    for (const auto &rel : deletes)
    {
        backupPaths.push_back(rel);
    }
    for (const auto &rel : backupPaths)
    {
        const std::string dir = ParentDir(rel);
        nsh << "  ${If} ${FileExists} \"$INSTDIR\\" << rel << "\"\n";
        if (!dir.empty())
        {
            nsh << "    CreateDirectory \"$INSTDIR\\update_backup\\" << fromVersion << "\\"
                << dir << "\"\n";
            nsh << "    CopyFiles /SILENT \"$INSTDIR\\" << rel << "\" \"$INSTDIR\\update_backup\\"
                << fromVersion << "\\" << dir << "\\\"\n";
        }
        else
        {
            nsh << "    CreateDirectory \"$INSTDIR\\update_backup\\" << fromVersion << "\"\n";
            nsh << "    CopyFiles /SILENT \"$INSTDIR\\" << rel << "\" \"$INSTDIR\\update_backup\\"
                << fromVersion << "\\\"\n";
        }
        nsh << "  ${EndIf}\n";
    }
    nsh << "\n";

    // 按目标目录分组写文件
    std::map<std::string, std::vector<FileEntry>> byDir;
    for (const auto &entry : changed)
    {
        byDir[ParentDir(entry.path)].push_back(entry);
    }
    int verifyIndex = 0;
    for (const auto &kv : byDir)
    {
        nsh << "  SetOutPath \"$INSTDIR";
        if (!kv.first.empty())
        {
            nsh << "\\" << kv.first;
        }
        nsh << "\"\n";
        for (const auto &entry : kv.second)
        {
            const fs::path source = targetRoot / ToBackslash(entry.path);
            nsh << "  File \"" << NsisText(source.string()) << "\"\n";
            ++verifyIndex;
            nsh << "  nsExec::ExecToStack 'cmd /c certutil -hashfile \"$INSTDIR\\"
                << entry.path << "\" SHA256 | findstr /v \":\"'\n";
            nsh << "  Pop $0\n";
            nsh << "  Pop $1\n";
            nsh << "  !insertmacro DELTA_TRIM_TAIL $1\n";
            nsh << "  StrCmp $1 \"" << entry.sha256 << "\" delta_hash_ok_"
                << verifyIndex << "\n";
            nsh << "    !insertmacro DELTA_ABORT \"sha256: " << entry.path << "\"\n";
            nsh << "  delta_hash_ok_" << verifyIndex << ":\n";
        }
    }
    nsh << "\n";

    if (!deletes.empty())
    {
        nsh << "  ; 删除新版已不存在的文件\n";
        for (const auto &rel : deletes)
        {
            nsh << "  Delete \"$INSTDIR\\" << rel << "\"\n";
        }
        nsh << "\n";
    }
    nsh << "  SetOverwrite ifnewer\n";
    nsh << "  DetailPrint \"增量更新完成：" << fromVersion << " -> " << toVersion << "\"\n";
    return nsh.str();
}

// ---------- 版本比较 ----------

std::vector<int> ParseVersion(const std::string &version)
{
    std::vector<int> parts;
    std::string cur;
    for (char ch : version)
    {
        if (ch == '.')
        {
            parts.push_back(cur.empty() ? 0 : std::atoi(cur.c_str()));
            cur.clear();
        }
        else if (std::isdigit(static_cast<unsigned char>(ch)))
        {
            cur.push_back(ch);
        }
        else if (!cur.empty())
        {
            break;
        }
    }
    if (!cur.empty())
    {
        parts.push_back(std::atoi(cur.c_str()));
    }
    while (parts.size() < 3)
    {
        parts.push_back(0);
    }
    return parts;
}

bool VersionLess(const std::string &lhs, const std::string &rhs)
{
    return ParseVersion(lhs) < ParseVersion(rhs);
}

// ---------- 任务 ----------

int CreateJob(const std::shared_ptr<drogon::orm::DbClient> &db, const std::string &fromVersion,
              const std::string &toVersion)
{
    auto rows = db->execSqlSync(
        "insert into update_delta_job (from_version, to_version, status, message) "
        "values ($1, $2, 0, '') returning id",
        fromVersion, toVersion);
    return rows.empty() ? 0 : rows[0]["id"].as<int>();
}

void UpdateJob(const std::shared_ptr<drogon::orm::DbClient> &db, int jobId, int status,
               const std::string &message)
{
    db->execSqlSync(
        "update update_delta_job set status = $2::int, message = $3, updated_at = now() where id = $1",
        jobId, status, message);
}

void FailJob(const std::shared_ptr<drogon::orm::DbClient> &db, int jobId, const std::string &message)
{
    try
    {
        UpdateJob(db, jobId, JobResult::Failed, message);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "更新差量任务状态失败: " << e.what();
    }
}

struct InstallerRow
{
    int id = 0;
    std::string version;
    std::string filePath;
    int status = 0;
    int64_t buildNo = 0;
};

bool LoadInstaller(const std::shared_ptr<drogon::orm::DbClient> &db, int installerId,
                   InstallerRow &out)
{
    auto rows = db->execSqlSync(
        "select id, version, coalesce(file_path, '') as file_path, status, "
        "       coalesce(build_no, 0) as build_no "
        "from installer_version where id = $1",
        installerId);
    if (rows.empty())
    {
        return false;
    }
    out.id = rows[0]["id"].as<int>();
    out.version = rows[0]["version"].as<std::string>();
    out.filePath = rows[0]["file_path"].as<std::string>();
    out.status = rows[0]["status"].as<int>();
    out.buildNo = rows[0]["build_no"].as<int64_t>();
    return !out.filePath.empty();
}

bool PickBaseInstaller(const std::shared_ptr<drogon::orm::DbClient> &db,
                       const InstallerRow &target, InstallerRow &base)
{
    auto rows = db->execSqlSync(
        "select id, version, coalesce(file_path, '') as file_path, status, "
        "       coalesce(build_no, 0) as build_no "
        "from installer_version "
        "where id <> $1 and coalesce(file_path, '') <> '' "
        "order by (status = 1) desc, id desc limit 20",
        target.id);
    bool found = false;
    for (const auto &row : rows)
    {
        InstallerRow candidate;
        candidate.id = row["id"].as<int>();
        candidate.version = row["version"].as<std::string>();
        candidate.filePath = row["file_path"].as<std::string>();
        candidate.status = row["status"].as<int>();
        candidate.buildNo = row["build_no"].as<int64_t>();
        if (candidate.version == target.version)
        {
            continue;
        }
        if (!VersionLess(candidate.version, target.version))
        {
            continue;
        }
        base = candidate;
        found = true;
        break;
    }
    return found;
}

bool DeltaExists(const std::shared_ptr<drogon::orm::DbClient> &db, const std::string &fromVersion,
                 const std::string &toVersion)
{
    auto rows = db->execSqlSync(
        "select 1 from update_delta where from_version = $1 and to_version = $2",
        fromVersion, toVersion);
    return !rows.empty();
}

std::string ResolveToolPath(const std::string &configured)
{
    // 配置成绝对路径就直接用；否则交给 PATH
    return configured;
}

void RunJob(int installerId)
{
    CleanupStaleWorkDirs();
    auto db = drogon::app().getDbClient();
    InstallerRow target;
    if (!LoadInstaller(db, installerId, target))
    {
        LOG_WARN << "差量自动生成: 全量包不存在或文件缺失, id=" << installerId;
        return;
    }

    InstallerRow base;
    if (!PickBaseInstaller(db, target, base))
    {
        const int jobId = CreateJob(db, "", target.version);
        if (jobId > 0)
        {
            UpdateJob(db, jobId, JobResult::Skipped, "没有可用的基线全量版本，跳过差量生成");
        }
        return;
    }

    if (DeltaExists(db, base.version, target.version))
    {
        const int jobId = CreateJob(db, base.version, target.version);
        if (jobId > 0)
        {
            UpdateJob(db, jobId, JobResult::Skipped, "该基线-目标版本的差量包已存在");
        }
        return;
    }

    const int jobId = CreateJob(db, base.version, target.version);
    if (jobId <= 0)
    {
        return;
    }

    try
    {
        UpdateJob(db, jobId, 1, "解包新旧安装器");

        const std::string uuid = drogon::utils::getUuid();
        const fs::path work = DeltaTempBase() / ("ueap-delta-" + uuid);
        struct TempGuard
        {
            fs::path path;
            ~TempGuard()
            {
                std::error_code ec;
                fs::remove_all(path, ec);
            }
        } guard{work};

        const fs::path baseRoot = work / "base";
        const fs::path targetRoot = work / "target";
        std::error_code ec;
        fs::create_directories(work, ec);
        const auto spaceInfo = fs::space(work, ec);
        if (!ec && spaceInfo.available < 10ULL * 1024 * 1024 * 1024)
        {
            FailJob(db, jobId, "临时目录可用空间不足 10GB，无法解包两份安装树");
            return;
        }
        ec.clear();
        fs::create_directories(baseRoot, ec);
        fs::create_directories(targetRoot, ec);

        const std::string sevenZip = ResolveToolPath(SevenZipPath());
        std::string output;
        const fs::path baseExe = fs::absolute(base.filePath, ec);
        const fs::path targetExe = fs::absolute(target.filePath, ec);
        if (ec || !fs::exists(baseExe) || !fs::exists(targetExe))
        {
            FailJob(db, jobId, "安装器文件不存在，无法解包");
            return;
        }

        int rc = RunCommand(NicePrefix() + Quote(sevenZip) + " x -y -o" + Quote(baseRoot.string()) + " " +
                                Quote(baseExe.string()),
                            output);
        if (rc != 0)
        {
            FailJob(db, jobId, "解包基线安装器失败: " + output.substr(0, 500));
            return;
        }
        rc = RunCommand(NicePrefix() + Quote(sevenZip) + " x -y -o" + Quote(targetRoot.string()) + " " +
                            Quote(targetExe.string()),
                        output);
        if (rc != 0)
        {
            FailJob(db, jobId, "解包目标安装器失败: " + output.substr(0, 500));
            return;
        }

        UpdateJob(db, jobId, 1, "比对文件树");
        std::vector<FileEntry> baseFiles;
        std::vector<FileEntry> targetFiles;
        std::string err;
        if (!CollectManifest(baseRoot, baseFiles, err) ||
            !CollectManifest(targetRoot, targetFiles, err))
        {
            FailJob(db, jobId, err);
            return;
        }

        std::map<std::string, FileEntry> baseMap;
        for (const auto &entry : baseFiles)
        {
            baseMap[Lower(entry.path)] = entry;
        }
        std::map<std::string, FileEntry> targetMap;
        for (const auto &entry : targetFiles)
        {
            targetMap[Lower(entry.path)] = entry;
        }

        const std::vector<std::string> groups = ForceGroups();
        std::vector<FileEntry> changed;
        std::vector<std::string> deletes;
        std::vector<std::string> forceTouched;
        int64_t changedBytes = 0;

        for (const auto &entry : targetFiles)
        {
            const auto it = baseMap.find(Lower(entry.path));
            const bool isChanged = (it == baseMap.end()) || it->second.sha256 != entry.sha256 ||
                                   it->second.size != entry.size;
            if (!isChanged)
            {
                continue;
            }
            changed.push_back(entry);
            changedBytes += entry.size;
            for (const auto &g : groups)
            {
                const std::string key = Lower(entry.path);
                const std::string prefix = Lower(g);
                if ((key == prefix || key.rfind(prefix + "\\", 0) == 0) &&
                    std::find(forceTouched.begin(), forceTouched.end(), g) == forceTouched.end())
                {
                    forceTouched.push_back(g);
                }
            }
        }
        for (const auto &entry : baseFiles)
        {
            if (targetMap.find(Lower(entry.path)) == targetMap.end())
            {
                deletes.push_back(entry.path);
                for (const auto &g : groups)
                {
                    const std::string key = Lower(entry.path);
                    const std::string prefix = Lower(g);
                    if (key == prefix || key.rfind(prefix + "\\", 0) == 0)
                    {
                        if (std::find(forceTouched.begin(), forceTouched.end(), g) ==
                            forceTouched.end())
                        {
                            forceTouched.push_back(g);
                        }
                    }
                }
            }
        }

        if (changed.empty() && deletes.empty())
        {
            UpdateJob(db, jobId, JobResult::Skipped, "两个版本安装树完全一致，无需差量包");
            return;
        }
        if (!forceTouched.empty())
        {
            std::string text = "强制整组替换目录有变化（";
            for (size_t i = 0; i < forceTouched.size(); ++i)
            {
                if (i > 0)
                {
                    text += "、";
                }
                text += forceTouched[i];
            }
            text += "），按策略只发全量包";
            UpdateJob(db, jobId, JobResult::Skipped, text);
            return;
        }

        UpdateJob(db, jobId, 1, "生成差量安装器");
        const fs::path nshPath = work / "delta_files.nsh";
        {
            std::ofstream nsh(nshPath, std::ios::binary | std::ios::trunc);
            if (!nsh)
            {
                FailJob(db, jobId, "写入 delta_files.nsh 失败");
                return;
            }
            nsh.write("\xEF\xBB\xBF", 3);   // NSIS 3 需要 BOM 才按 UTF-8 解析
            const std::string content =
                GenerateDeltaNsh(base.version, target.version, targetRoot, changed, deletes, baseMap);
            nsh.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        const std::string assets = AssetsDir();
        const fs::path nsiPath = fs::path(assets) / "AutoPDMS-project-setup.nsi";
        const fs::path licensePath = fs::path(assets) / "UESoftMYRMAgreement.txt";
        if (!fs::exists(nsiPath) || !fs::exists(licensePath))
        {
            FailJob(db, jobId, "服务端缺失 NSIS 模板或许可证文件: " + assets);
            return;
        }

        const fs::path deltaDir = fs::path(StoreDir()) / "delta" /
                                  (target.version + "_from_" + base.version);
        fs::create_directories(deltaDir, ec);
        if (ec)
        {
            FailJob(db, jobId, "创建差量存储目录失败: " + ec.message());
            return;
        }
        const fs::path deltaExe = deltaDir /
            ("UEAP_PRJ_" + target.version + "_Delta_from_" + base.version + "_Win32.exe");
        fs::remove(deltaExe, ec);

        const std::vector<int> toParts = ParseVersion(target.version);
        std::string makensis;
        makensis += NicePrefix();
        makensis += Quote(ResolveToolPath(MakensisPath()));
        makensis += " -V2";
        makensis += " " + Quote("-DSETUP_DIR=" + fs::absolute(deltaDir, ec).string());
        makensis += " -DDELTA_MODE";
        makensis += " " + Quote("-DDELTA_FROM_VERSION=" + base.version);
        makensis += " " + Quote("-DDELTA_INCLUDE=" + nshPath.string());
        makensis += " " + Quote("-DPRODUCT_SOURCE_DIR_ROOT=" + targetRoot.string());
        makensis += " " + Quote("-DMYRM_LICENSE=" + fs::absolute(licensePath, ec).string());
        makensis += " " + Quote("-DPRODUCT_MAJOR_VERSION=" + std::to_string(toParts[0]));
        makensis += " " + Quote("-DPRODUCT_MINOR_VERSION=" + std::to_string(toParts[1]));
        makensis += " " + Quote("-DPRODUCT_PATCH_VERSION=" + std::to_string(toParts[2]));
        makensis += " " + Quote(fs::absolute(nsiPath, ec).string());

        output.clear();
        rc = RunCommand(makensis, output);
        if (rc != 0 || !fs::exists(deltaExe))
        {
            FailJob(db, jobId, "编译差量安装器失败: " + output.substr(0, 800));
            return;
        }

        int64_t deltaSize = 0;
        const std::string deltaSha = Sha256FileHex(deltaExe, deltaSize);

        Json::Value manifest(Json::objectValue);
        manifest["schemaVersion"] = 1;
        manifest["fromVersion"] = base.version;
        manifest["toVersion"] = target.version;
        manifest["buildNo"] = static_cast<Json::Int64>(target.buildNo);
        manifest["generatedAt"] = NowText();
        manifest["origin"] = "auto";
        manifest["totalInstallBytes"] = static_cast<Json::Int64>(changedBytes);
        Json::Value files(Json::arrayValue);
        for (const auto &entry : changed)
        {
            Json::Value item;
            item["path"] = entry.path;
            item["size"] = static_cast<Json::Int64>(entry.size);
            item["sha256"] = entry.sha256;
            item["forceGroup"] = false;
            files.append(item);
        }
        manifest["files"] = files;
        Json::Value deleteArray(Json::arrayValue);
        for (const auto &rel : deletes)
        {
            deleteArray.append(rel);
        }
        manifest["deletes"] = deleteArray;
        manifest["forceGroups"] = Json::Value(Json::arrayValue);
        Json::Value baseSample(Json::arrayValue);
        for (const auto &entry : PickBaseFiles(baseFiles))
        {
            Json::Value item;
            item["path"] = entry.path;
            item["sha256"] = entry.sha256;
            baseSample.append(item);
        }
        manifest["baseFiles"] = baseSample;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string manifestText = Json::writeString(builder, manifest);

        const std::string deltaFileName = deltaExe.filename().string();
        const std::string deltaPathText = deltaExe.string();
        auto rows = db->execSqlSync(
            "insert into update_delta "
            "(from_version, to_version, build_no, file_name, file_path, size_bytes, sha256, "
            " manifest, status, origin, published_at) "
            "values ($1, $2, $3::bigint, $4, $5, $6::bigint, $7, $8::jsonb, 0, 'auto', null) "
            "returning id",
            base.version, target.version, target.buildNo, deltaFileName,
            deltaPathText, deltaSize, deltaSha, manifestText);

        if (rows.empty())
        {
            std::error_code removeEc;
            fs::remove(deltaExe, removeEc);
            FailJob(db, jobId, "写入差量记录失败");
            return;
        }

        // 全量可能在上传后、任务完成前被发布：统一走一次"随全量发布"
        DeltaBuilder::PublishAutoDelta(target.version);

        std::ostringstream done;
        done << "差量包已生成（" << (deltaSize / 1048576.0) << " MB，"
             << changed.size() << " 个变更文件，"
             << deletes.size() << " 个删除）";
        UpdateJob(db, jobId, JobResult::Done, done.str());
        LOG_INFO << "差量包自动生成完成: " << base.version << " -> " << target.version
                 << ", size=" << deltaSize;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "差量自动生成异常: " << e.what();
        FailJob(db, jobId, e.what());
    }
}

std::mutex g_queueMutex;
std::condition_variable g_queueCv;
std::deque<int> g_queue;
std::once_flag g_workerOnce;

void ResetInterruptedJobs()
{
    try
    {
        auto db = drogon::app().getDbClient();
        db->execSqlSync(
            "update update_delta_job set status = 3, "
            "message = '服务重启，任务中断，可在管理页点「重新生成」', updated_at = now() "
            "where status in (0, 1)");
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "重置中断差量任务失败: " << e.what();
    }
}

void WorkerLoop()
{
    ResetInterruptedJobs();
    CleanupStaleWorkDirs();
    while (true)
    {
        int installerId = 0;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCv.wait(lock, [] { return !g_queue.empty(); });
            installerId = g_queue.front();
            g_queue.pop_front();
        }
        try
        {
            RunJob(installerId);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "差量任务线程异常: " << e.what();
        }
    }
}

} // namespace

// ---------- 对外接口 ----------

void DeltaBuilder::EnqueueAutoBuild(int installerId)
{
    if (!AutoEnabled())
    {
        LOG_INFO << "差量自动生成已关闭（Installer.DeltaAuto=false），跳过 id=" << installerId;
        return;
    }
    std::call_once(g_workerOnce, [] { std::thread(WorkerLoop).detach(); });
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_queue.push_back(installerId);
    }
    g_queueCv.notify_one();
}

void DeltaBuilder::PublishAutoDelta(const std::string &toVersion)
{
    try
    {
        auto db = drogon::app().getDbClient();

        // 30 分钟没有更新的任务视为卡住，标记失败，避免"有活跃任务"永久阻止重新生成
        db->execSqlSync(
            "update update_delta_job set status = 3, "
            "message = '任务超过 30 分钟未更新，已标记失败', updated_at = now() "
            "where status in (0, 1) and updated_at < now() - interval '30 minutes'");

        // 先看有没有正在跑的任务，避免发布时重复排队
        auto activeRows = db->execSqlSync(
            "select 1 from update_delta_job where to_version = $1 and status in (0, 1) limit 1",
            toVersion);
        const bool hasActive = !activeRows.empty();

        db->execSqlSync(
            "update update_delta set status = 1, published_at = now() "
            "where to_version = $1 and status = 0 and origin = 'auto'",
            toVersion);

        auto existRows = db->execSqlSync(
            "select 1 from update_delta where to_version = $1 limit 1", toVersion);
        if (existRows.empty() && !hasActive)
        {
            auto idRows = db->execSqlSync(
                "select id from installer_version where version = $1 order by id desc limit 1",
                toVersion);
            if (!idRows.empty())
            {
                EnqueueAutoBuild(idRows[0]["id"].as<int>());
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "发布自动差量失败: " << e.what();
    }
}

void DeltaBuilder::RemoveAutoDeltasForVersion(const std::string &version)
{
    try
    {
        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select id, from_version, to_version, coalesce(file_path, '') as file_path "
            "from update_delta where origin = 'auto' and (from_version = $1 or to_version = $1)",
            version);
        for (const auto &row : rows)
        {
            const std::string fromVersion = row["from_version"].as<std::string>();
            const std::string toVersion = row["to_version"].as<std::string>();
            const std::string filePath = row["file_path"].as<std::string>();
            db->execSqlSync("delete from update_delta where id = $1", row["id"].as<int>());
            std::error_code ec;
            if (!filePath.empty())
            {
                fs::remove(filePath, ec);
            }
            fs::remove_all(fs::path(StoreDir()) / "delta" /
                               (toVersion + "_from_" + fromVersion), ec);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "清理自动差量失败: " << e.what();
    }
}

HttpResult DeltaBuilder::AdminListJobs(int limit)
{
    HttpResult result;
    if (limit <= 0 || limit > 200)
    {
        limit = 50;
    }
    try
    {
        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select id, from_version, to_version, status, message, "
            "       to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at, "
            "       to_char(updated_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as updated_at "
            "from update_delta_job order by id desc limit $1::int",
            limit);
        Json::Value items(Json::arrayValue);
        for (const auto &row : rows)
        {
            Json::Value item;
            item["id"] = row["id"].as<int>();
            item["fromVersion"] = row["from_version"].as<std::string>();
            item["toVersion"] = row["to_version"].as<std::string>();
            item["status"] = row["status"].as<int>();
            item["message"] = row["message"].as<std::string>();
            item["createdAt"] = row["created_at"].isNull() ? "" : row["created_at"].as<std::string>();
            item["updatedAt"] = row["updated_at"].isNull() ? "" : row["updated_at"].as<std::string>();
            items.append(item);
        }
        result.jsondata["jobs"] = items;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "AdminListJobs 失败: " << e.what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult DeltaBuilder::AdminHealth()
{
    HttpResult result;
    Json::Value data;
    data["autoEnabled"] = AutoEnabled();

    const std::string assets = AssetsDir();
    const fs::path nsiPath = fs::path(assets) / "AutoPDMS-project-setup.nsi";
    const fs::path licensePath = fs::path(assets) / "UESoftMYRMAgreement.txt";
    Json::Value missing(Json::arrayValue);
    if (!fs::exists(nsiPath))
    {
        missing.append("AutoPDMS-project-setup.nsi");
    }
    if (!fs::exists(licensePath))
    {
        missing.append("UESoftMYRMAgreement.txt");
    }
    data["assetsDir"] = assets;
    data["assetsOk"] = missing.empty();
    data["assetsMissing"] = missing;

    auto checkTool = [](const std::string &name, const std::string &versionArg, Json::Value &out) {
        std::string output;
        const int rc = RunCommand(Quote(name) + " " + versionArg, output);
        out["path"] = name;
        out["ok"] = (rc == 0);
        std::string first;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line != "\r")
            {
                first = line;
                break;
            }
        }
        if (first.size() > 200)
        {
            first = first.substr(0, 200);
        }
        out["version"] = first;
    };
    Json::Value sevenZip;
    checkTool(SevenZipPath(), "i", sevenZip);
    Json::Value makensis;
    checkTool(MakensisPath(), "-VERSION", makensis);
    data["sevenZip"] = sevenZip;
    data["makensis"] = makensis;

    std::string tempBase = ConfigString("DeltaTempDir", "");
    if (tempBase.empty())
    {
        if (const char *env = std::getenv("UEADMIN_DELTA_TEMP_DIR"))
        {
            tempBase = env;
        }
    }
    const fs::path tempDir = tempBase.empty() ? fs::temp_directory_path() : fs::path(tempBase);
    std::error_code ec;
    fs::create_directories(tempDir, ec);
    const auto spaceInfo = fs::space(tempDir, ec);
    data["tempDir"] = tempDir.string();
    const bool spaceOk = !ec && spaceInfo.available >= 10ULL * 1024 * 1024 * 1024;
    if (!ec)
    {
        data["tempFreeBytes"] = static_cast<Json::Int64>(spaceInfo.available);
    }
    data["tempSpaceOk"] = spaceOk;

    Json::Value groups(Json::arrayValue);
    for (const auto &group : ForceGroups())
    {
        groups.append(group);
    }
    data["forceGroups"] = groups;

    const bool ok = AutoEnabled() && missing.empty() && sevenZip["ok"].asBool() &&
                    makensis["ok"].asBool() && spaceOk;
    data["ok"] = ok;
    result.jsondata = data;
    result.setResult(ApiErrorCode::ApiError_Success, ok ? "delta runtime ready" : "delta runtime not ready");
    return result;
}

HttpResult DeltaBuilder::AdminRebuild(const std::string &fromVersion, const std::string &toVersion)
{
    HttpResult result;
    if (!AutoEnabled())
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "差量自动生成已关闭");
        return result;
    }
    if (toVersion.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 toVersion");
        return result;
    }
    try
    {
        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select id from installer_version where version = $1 order by id desc limit 1",
            toVersion);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
            return result;
        }

        // 指定基线时先删除已有记录（含文件），便于重跑
        if (!fromVersion.empty())
        {
            auto oldRows = db->execSqlSync(
                "select id, coalesce(file_path, '') as file_path from update_delta "
                "where from_version = $1 and to_version = $2 and origin = 'auto'",
                fromVersion, toVersion);
            for (const auto &row : oldRows)
            {
                db->execSqlSync("delete from update_delta where id = $1", row["id"].as<int>());
                std::error_code ec;
                fs::remove(row["file_path"].as<std::string>(), ec);
            }
        }

        EnqueueAutoBuild(rows[0]["id"].as<int>());
        result.jsondata["queued"] = true;
        result.jsondata["toVersion"] = toVersion;
        result.setResult(ApiErrorCode::ApiError_Success, "已加入差量生成队列");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "AdminRebuild 失败: " << e.what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

} // namespace Services
} // namespace UEAdminAPI
