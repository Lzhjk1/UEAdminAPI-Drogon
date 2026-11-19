Unicode true
; #############################################################################
; # AutoPDMS 安装程序脚本
; # 用于生成 UESoft AutoPDMS 项目安装包，包含各种CAD平台和VC运行库的文件
; # 版本号由 PRODUCT_MAJOR/MINOR/PATCH 宏定义（主.次.修订），构建日期另见 PRODUCT_BUILD_DATE
; # 基于 HM NIS Edit Script Wizard 生成
; # 最后更新日期：2025-12-08
; #############################################################################
;


; #############################################################################
; # 全局变量与宏定义区域
; #############################################################################
; HM NIS Edit Wizard helper defines
; 作者修改记录：liyunfan on 2023-06-01

; 安装程序输出目录（包含版本号+日期+构建号）
!ifndef SETUP_DIR
!define SETUP_DIR "d:\AutoPDMS安装包"
!endif
; 安装程序输出文件名（包含产品名+主/次/修订版本+日期时间）
!define SETUP_EXE "${SETUP_DIR}\UEAP_PRJ_${PRODUCT_MAJOR_VERSION}.${PRODUCT_MINOR_VERSION}.${PRODUCT_PATCH_VERSION}_Setup.Win32.build_${PRODUCT_DATE}.exe"
; 产品源文件根目录（包含所有需要打包的文件）
!ifndef PRODUCT_SOURCE_DIR_ROOT
!define PRODUCT_SOURCE_DIR_ROOT "D:\UEInstallPack\ObjectPRX-PackFile\AutoPDS"
!endif
; 支持项目文件打包的标签（用于条件编译）
!define TAG_SUPPORT_PROJECT_FILES "TAG_SUPPORT_PROJECT_FILES"

; 作者修改记录：ligb on 2022-08-16
!define COMPANY_NAME "UESoft"                     ; 公司名称
!define PRODUCT_NAME "UESoft AutoPDMS"           ; 产品名称
;!define PRODUCT_NAME "UESoft Studio"             ; 产品备用名称（已注释）
; 产品主版本号和次版本号
!ifndef PRODUCT_MAJOR_VERSION
!define PRODUCT_MAJOR_VERSION "11"
!endif
!ifndef PRODUCT_MINOR_VERSION
!define PRODUCT_MINOR_VERSION "8"
!endif
!ifndef PRODUCT_PATCH_VERSION
!define PRODUCT_PATCH_VERSION "0"
!endif              ; 产品修订版本号（安装包名使用；安装目录不再带版本号）
; 产品构建日期，格式为YYYYMMDD
!define /date PRODUCT_BUILD_DATE %Y%m%d
; 产品完整版本号，格式为：主版本.次版本.修订版本
!define PRODUCT_VERSION "${PRODUCT_MAJOR_VERSION}.${PRODUCT_MINOR_VERSION}.${PRODUCT_PATCH_VERSION}"          ; 产品版本号
!define PRODUCT_BACKUP_VERSION "0.1"              ; 产品备份版本号
!define PRODUCT_PUBLISHER "UESoft Corp."          ; 发布商名称
!define PRODUCT_PUBLISHER_WEB "http://www.uesoft.com" ; 发布商网站
!define PRODUCT_WEB_SITE "http://autopdms.uesoft.com" ; 产品官网

; #############################################################################
; # 子产品名称定义
; #############################################################################
!define SUB_PRODUCT_NAME_UESS "${PRODUCT_NAME}"         ; UESS 产品名称
!define SUB_PRODUCT_NAME_AP "AutoPDMS"                  ; AutoPDMS 产品名称
!define SUB_PRODUCT_NAME_CB "CodeBlocks"                ; CodeBlocks 产品名称
!define SUB_PRODUCT_NAME_NINGW "MinGW"                  ; MinGW 产品名称
!define SUB_PRODUCT_NAME_CMAKE  "CMake"                 ; CMake 产品名称
!define SUB_PRODUCT_NAME_GTK    "GTK"                   ; GTK 产品名称
!define SUB_PRODUCT_NAME_Qt    "Qt"                     ; Qt 产品名称
!define SUB_PRODUCT_NAME_GIT   "GIT"                    ; GIT 产品名称
!define SUB_PRODUCT_NAME_GOGS   "GOGS"                  ; GOGS 产品名称
!define SUB_PRODUCT_NAME_UnrealIRCd "UnrealIRCd"        ; UnrealIRCd 产品名称
!define SUB_PRODUCT_NAME_Pidgin "Pidgin"                ; Pidgin 产品名称
!define SUB_PRODUCT_NAME_Lilishop "Lilishop"            ; Lilishop 产品名称
!define SUB_PRODUCT_NAME_Material "Material"            ; Material 产品名称
!define SUB_PRODUCT_SRC_DIR_PIDGIN "pidgin-install-dir" ; Pidgin 客户端在打包树中的目录名
!define SUB_PRODUCT_PATH_PIDGIN "${SUB_PRODUCT_NAME_Pidgin}" ; Pidgin 安装子目录，即 $INSTDIR\Pidgin

; #############################################################################
; # WaterPro 水利软件相关定义
; #############################################################################
!define PRODUCT_NAME_WATERPRO "UESoft WaterPro"                      ; WaterPro 产品名称
!define SUB_PRODUCT_NAME_WATERPRO "WaterPro"                         ; WaterPro 子产品名称
!define SUB_PRODUCT_NAME_WATERPRO_VERSION "8.0"                      ; WaterPro 版本号
!define SUB_PRODUCT_PATH_WATERPRO_DESKTOP "${COMPANY_NAME}\${PRODUCT_NAME_WATERPRO}\${SUB_PRODUCT_NAME_WATERPRO_VERSION}\${SUB_PRODUCT_NAME_WATERPRO}" ; 桌面快捷方式路径
!define SUB_PRODUCT_PATH_WATERPRO_EXE_DIR "${SUB_PRODUCT_NAME_Material}\${SUB_PRODUCT_NAME_WATERPRO}\${SUB_PRODUCT_NAME_WATERPRO_VERSION}\${SUB_PRODUCT_NAME_WATERPRO}" ; 安装目录路径
!define SUB_PRODUCT_NAME_WATERPRO_EXE "IFC97IFC67.exe"               ; WaterPro 主程序可执行文件名
!define PRODUCT_NAME_WATERPRO_EXE "${PRODUCT_NAME_WATERPRO}${SUB_PRODUCT_NAME_WATERPRO_VERSION}" ; WaterPro 完整产品名称

; #############################################################################
; # 主程序相关定义
; #############################################################################
!define PRODUCT_MAIN_APP_NAME "APPDMSLogin10"           ; 主程序可执行文件名
!define PRODUCT_MAIN_APP_EXT ".exe"                    ; 主程序文件扩展名
!define PRODUCT_MAIN_APP_NAME_EXT "${PRODUCT_MAIN_APP_NAME}${PRODUCT_MAIN_APP_EXT}" ; 主程序完整文件名

; #############################################################################
; # 注册表相关定义
; #############################################################################
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\${PRODUCT_MAIN_APP_NAME_EXT}" ; 主程序路径注册表键
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" ; 卸载信息注册表键
!define PRODUCT_UNINST_ROOT_KEY "HKLM"                  ; 卸载信息注册表根键
!define PRODUCT_REG_DIR "SOFTWARE\Wow6432Node\UESoft Shared\${SUB_PRODUCT_NAME_AP}${PRODUCT_VERSION}" ; AutoPDMS 版本注册表路径
!define PRODUCT_REG_DIR_AUTOPDMS "SOFTWARE\Wow6432Node\UESoft Shared\${SUB_PRODUCT_NAME_AP}" ; AutoPDMS 通用注册表路径
; VC运行库"本机只装一次"标记键。安装程序是 32 位，写 HKLM\SOFTWARE 会被 WOW64 重定向到
; Wow6432Node 视图，本键的读与写都在同一视图内（脚本只读写这一套键），故此处不写死 Wow6432Node。
; 卸载段刻意不删除此键：VC 运行库本身不在卸载范围内，卸载后重装不应再跑一遍合集安装程序。
; 如需强制重装，删除该键下的 Installed 值后重新安装即可。
!define PRODUCT_REG_DIR_VCRUNTIME "SOFTWARE\UESoft Shared\VCRuntime" ; VC运行库只装一次标记键

; #############################################################################
; # 子产品应用程序名称定义
; #############################################################################
!define SUB_PRODUCT_NAME_AutoPDMSLogin "APPDMSLogin10"  ; AutoPDMS 登录程序名称
!define SUB_PRODUCT_NAME_AutoPSA "AutoPSA"              ; AutoPSA 产品名称
!define SUB_PRODUCT_NAME_PHS3D "PHS3D"                  ; PHS3D 产品名称
!define SUB_PRODUCT_NAME_AutoIPED "AutoIPED"            ; AutoIPED 产品名称
!define SUB_PRODUCT_NAME_EP3D "EP3D"                    ; EP3D 产品名称
!define SUB_PRODUCT_NAME_scicoslab "scicoslab"          ; scicoslab 产品名称
!define SUB_PRODUCT_16_APP_NAME "woodem"                ; 第16个应用程序名称
!define SUB_PRODUCT_17_APP_NAME "bleed"                 ; 第17个应用程序名称
!define SUB_PRODUCT_18_APP_NAME "lammaps"               ; 第18个应用程序名称
!define SUB_PRODUCT_19_APP_NAME "stellarium"            ; 第19个应用程序名称

!define COMPANY_PRODUCT_NAME "${COMPANY_NAME}\${PRODUCT_NAME}"
!define COMPANY_PRODUCT_NAME_VERSION "${COMPANY_PRODUCT_NAME}\${PRODUCT_VERSION}"
!define COMPANY_PRODUCT_NAME_BACKOP_VERSION "${COMPANY_PRODUCT_NAME}\${PRODUCT_BACKUP_VERSION}"

; #############################################################################
; # 子目录名称定义
; #############################################################################
;!define SUB_DIR_BIN "bin"                        ; 二进制文件目录（已注释）
!define SUB_DIR_DLL "cpp-2005-win32"             ; DLL 目录名称
!define SUB_DIR_XRX "AutoCAD-2007-win32"         ; AutoCAD 插件目录
;!define SUB_DIR_DLL "cpp-2010-win32"             ; DLL 目录备用名称（已注释）
;!define SUB_DIR_XRX "ZWCAD-2020-win32"           ; ZWCAD 插件目录备用名称（已注释）
!define SUB_DIR_DLL_ZW "cpp-2010-win32"          ; ZWCAD DLL 目录
!define SUB_DIR_XRX_ZW "ZWCAD-2020-win32"        ; ZWCAD 插件目录

!define SUB_DIR_system "system"                  ; 系统文件目录
!define SUB_DIR_config "config"                  ; 配置文件目录
!define SUB_DIR_project "project"                ; 项目文件目录

; #############################################################################
; # 安装目录路径定义
; #############################################################################
!define AP_INSDIR "${COMPANY_PRODUCT_NAME}\${SUB_PRODUCT_NAME_AP}"   ; AutoPDMS 安装目录（不带版本号）

!define AP_BIN_INSDIR "${AP_INSDIR}"             ; AutoPDMS 二进制文件安装目录

!define APPDMSLOGIN_INSDIR "${AP_BIN_INSDIR}"    ; APPDMSLogin 安装目录

!define CB_INSDIR "${COMPANY_PRODUCT_NAME_VERSION}\${SUB_PRODUCT_NAME_CB}" ; CodeBlocks 安装目录

!define PRODUCT_NAME_VERSION "${PRODUCT_NAME} ${PRODUCT_VERSION}" ; 产品名称和版本号组合

; #############################################################################
; # MinGW 相关定义
; #############################################################################
;!define MINGW_VERSION "8.1.0"                    ; MinGW 版本号（备用）
!define MINGW_VERSION "5.3.0"                     ; MinGW 版本号
!define MINGW_NAME_VERSION "${SUB_PRODUCT_NAME_NINGW}${MINGW_VERSION}" ; 完整 MinGW 版本名称

!define /date PRODUCT_DATE %Y%m%d%H%M                 ; 产品构建日期时间，格式为YYYYMMDDHHMM


; #############################################################################
; # 安装程序权限设置
; #############################################################################
; 请求管理员权限运行安装程序 - 以兼容 Windows Vista 及以上系统的UAC机制
RequestExecutionLevel admin

; #############################################################################
; # 源文件路径定义
; #############################################################################
!define PRODUCT_SOURCE_CB20_03_ROOT "D:\Program Files\CodeBlocks"       ; CodeBlocks 安装目录
!define PRODUCT_SOURCE_UECS_ROOT "$PROGRAMFILES\${COMPANY_NAME}\${PRODUCT_NAME}\${PRODUCT_BACKUP_VERSION}" ; UECS 安装目录
!define PRODUCT_SOURCE_UECS_SRC_ROOT "d:\vc\CodeBlocks\src"           ; UECS 源代码目录
!define PRODUCT_SOURCE_UECS_SRC_OUTPUT_DIR "output30"                   ; UECS 输出目录
!define PRODUCT_SOURCE_QT5_9_0_ROOT "C:\Qt\Qt5.9.0"                   ; Qt 5.9.0 安装目录
!define MINGW_ROOT "${PRODUCT_SOURCE_QT5_9_0_ROOT}"                     ; MinGW 根目录
!define MINGW_DIR "${SUB_PRODUCT_NAME_NINGW}"                           ; MinGW 目录名
!define QTMINGW_DIR "Tools\mingw530_32"                               ; Qt 自带 MinGW 目录
; 许可协议文件路径
;!define MYRM_LICENSE "d:\vc\Computing\doc\UESoftMYRMAgreement.txt" ; 许可协议文件路径（备用）
!ifndef MYRM_LICENSE
!define MYRM_LICENSE "D:\vc\objectprx\AutoBuildPRX\UESoftMYRMAgreement.txt"
!endif ; 许可协议文件路径

; MinGW 相关路径
!define PRODUCT_SOURCE_MINGW530_32_ROOT "${PRODUCT_SOURCE_QT5_9_0_ROOT}\${QTMINGW_DIR}" ; MinGW 5.3.0 32位根目录
!define PRODUCT_SOURCE_MINGW810_64_ROOT "${PRODUCT_SOURCE_CB20_03_ROOT}\${MINGW_DIR}" ; MinGW 8.1.0 64位根目录

;!define PRODUCT_MINGW_SOURCE_DIR "${PRODUCT_SOURCE_MINGW810_64_ROOT}" ; MinGW 源文件目录64位（备用）
!define PRODUCT_MINGW_SOURCE_DIR "${PRODUCT_SOURCE_MINGW530_32_ROOT}" ; 默认使用 MinGW 源文件目录32位

; 产品源文件根目录定义
;!define PRODUCT_SOURCE_DIR_ROOT "${PRODUCT_SOURCE_UECS_ROOT}" ; 备用：使用UECS安装目录
;!define PRODUCT_SOURCE_DIR_ROOT "${PRODUCT_SOURCE_UECS_SRC_OUTPUT_DIR}" ; 备用：使用输出目录
;!define PRODUCT_SOURCE_DIR_ROOT "${PRODUCT_SOURCE_UECS_SRC_ROOT}\${PRODUCT_SOURCE_UECS_SRC_OUTPUT_DIR}" ; 备用：使用源代码输出目录
;!define PRODUCT_SOURCE_DIR_ROOT "D:\Program Files (x86)\中冶赛迪工程技术股份有限公司2\AutoPDMS11.5" ; 备用：使用特定安装目录

; DLL 和 EXE 源文件根目录
!define PRODUCT_SOURCE_DLLEXE_DIR_ROOT "D:\uelib\lib9"


; #############################################################################
; # 安装程序配置选项
; #############################################################################
BrandingText "@2022 ${PRODUCT_PUBLISHER}"     ; 安装程序右下角显示的发布商信息

AllowSkipFiles off                             ; 不允许跳过文件复制
SetOverwrite ifnewer                           ; 仅覆盖较新的文件
ShowInstDetails show                           ; 显示安装详细信息
Unicode true                                   ; 启用 Unicode 支持以正确处理中文字符
; 2026-11-19 压缩算法可由构建脚本 -Compressor 覆盖（默认 lzma；zlib 更快但包体明显变大）
!ifndef SETUP_COMPRESSOR
!define SETUP_COMPRESSOR "lzma"
!endif
SetCompressor  ${SETUP_COMPRESSOR}             ; 压缩算法：默认 LZMA；/DSETUP_COMPRESSOR=zlib 可换更快但更大的 zlib

; 安装程序基本设置
Name "${PRODUCT_NAME_VERSION}"                  ; 安装程序显示的名称
!ifdef DELTA_MODE
  !ifndef DELTA_FROM_VERSION
    !error "DELTA_MODE 需要 -DDELTA_FROM_VERSION=<基线版本>"
  !endif
  !ifndef DELTA_INCLUDE
    !error "DELTA_MODE 需要 -DDELTA_INCLUDE=<delta_files.nsh 路径>"
  !endif
  OutFile "${SETUP_DIR}\UEAP_PRJ_${PRODUCT_VERSION}_Delta_from_${DELTA_FROM_VERSION}_Win32.exe"
!else
  OutFile "${SETUP_EXE}"                         ; 安装程序输出文件名
!endif
InstallDir "$PROGRAMFILES\${AP_INSDIR}" ; 默认安装目录（不带版本号）
;InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" "" ; 从注册表读取上一次安装目录（已注释）
ShowInstDetails show                           ; 显示安装详细信息
ShowUnInstDetails show                         ; 显示卸载详细信息


; 自定义脚本包含（6） ligb on 2022-02-19
;!include "writeRegStr.nsh" ; 自定义注册表写入函数（已注释）
; 自定义脚本包含结束
; ligb on 2022-09-09

; #############################################################################
; # 头文件包含
; #############################################################################
!include "x64.nsh"                              ; 64位系统检测头文件
!include "WordFunc.nsh"                         ; 字符串处理函数头文件
!include "LogicLib.nsh"                         ; 逻辑库头文件，支持${If}等语法
!include "FileFunc.nsh"                                ; 目录体积统计（EstimatedSize）

; MUI 相关头文件
; MUI 1.67 compatible ------
;!include "MUI.nsh"                              ; MUI 1.67 版本头文件（已注释）
!include "MUI2.nsh"                             ; MUI 2.0 版本头文件

; #############################################################################
; # MUI 界面设置
; #############################################################################
; MUI 通用设置
!define MUI_ABORTWARNING                         ; 退出时显示警告对话框
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico" ; 安装程序图标
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico" ; 卸载程序图标

; 安装向导页面设置
; Welcome page - 欢迎页面（已注释）
;!insertmacro MUI_PAGE_WELCOME
; License page - 许可协议页面
!define MUI_LICENSEPAGE_CHECKBOX                 ; 许可协议页面显示复选框
!insertmacro MUI_PAGE_LICENSE "${MYRM_LICENSE}" ; 插入许可协议页面，指定许可协议文件
; Directory page - 安装目录选择页面
; 已安装过时沿用原目录：.onInit 检测到 $PreviousInstallDir 后，本页只展示、禁止修改
!define MUI_PAGE_CUSTOMFUNCTION_SHOW DirectoryPageShow
!insertmacro MUI_PAGE_DIRECTORY                 ; 插入目录选择页面
; Instfiles page - 安装文件复制页面
!insertmacro MUI_PAGE_INSTFILES                 ; 插入文件安装页面
; Finish page - 安装完成页面
!define MUI_FINISHPAGE_RUN "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}" ; 安装完成后运行主程序
!insertmacro MUI_PAGE_FINISH                    ; 插入完成页面

; 卸载向导页面设置
!insertmacro MUI_UNPAGE_INSTFILES               ; 插入卸载文件页面

; 语言设置
!insertmacro MUI_LANGUAGE "English"             ; 英文语言
!insertmacro MUI_LANGUAGE "SimpChinese"         ; 简体中文语言
;!insertmacro MULTIUSER_LANGUAGE_INIT            ; 多用户语言初始化（已注释）

; MUI 设置结束 ------

; 全局变量定义
Var /GLOBAL PreviousInstallDir ; 检测到的旧安装目录（空=首次安装，显示目录页）
Var /GLOBAL BITSNUM            ; 用于存储系统位数x86或x64
Var /GLOBAL vcredistFileName   ; 用于存储VC运行库安装文件名

; #############################################################################
; # 系统检测与初始化
; #############################################################################
Section
  ; 检测是否为64位系统
  ${If} ${RunningX64}
    ; 64位系统执行以下操作
    strcpy $BITSNUM "x64"                   ; 设置系统位数变量为x64
    strcpy $vcredistFileName "vcredist_x64vc8sp1.exe" ; 设置64位VC运行库安装文件名
  ${Else}
    ; 32位系统执行以下操作
    strcpy $BITSNUM "x86"                   ; 设置系统位数变量为x86
    strcpy $vcredistFileName "vcredist_x86vc8sp1.exe" ; 设置32位VC运行库安装文件名
  ${EndIf}
  
  ; 集成VC运行库安装文件，包含VCRUNTIME140.dll等运行库
  !define VCCollReFileName "微软VC运行库合集_2025.09.16_1758189813.exe" ; VC运行库集成安装文件名
  !define VCCollFN "${PRODUCT_SOURCE_DIR_ROOT}\${VCCollReFileName}" ; VC运行库集成安装文件完整路径
SectionEnd


; #############################################################################
; # project 覆盖安装判定（数据库版本比较）
; #############################################################################
; DbVersionToScaled: 输入 $R0（如 9.901 / 8.08 / 1），输出 $R9 = 版本 × 10000。
; 与 UpGradeProjOprate::VsMdbVersion 的 atof 语义一致：8.08 < 8.1。
Function DbVersionToScaled
  Push $R0
  Push $R1
  Push $R2
  Push $R3
  Push $R4

  ${WordFind} "$R0" "." "+1" $R1
  StrLen $R3 $R1
  StrCpy $R2 "$R0" "" $R3
  StrCpy $R4 $R2 1
  ${If} $R4 == "."
    StrCpy $R2 "$R2" "" 1
  ${Else}
    StrCpy $R2 ""
  ${EndIf}
  ${If} $R1 == ""
    StrCpy $R1 "0"
  ${EndIf}
  StrCpy $R2 $R2 4
  StrLen $R3 $R2
  ${If} $R3 == 0
    StrCpy $R4 0
  ${ElseIf} $R3 == 1
    IntOp $R4 $R2 * 1000
  ${ElseIf} $R3 == 2
    IntOp $R4 $R2 * 100
  ${ElseIf} $R3 == 3
    IntOp $R4 $R2 * 10
  ${Else}
    IntOp $R4 $R2 * 1
  ${EndIf}
  IntOp $R1 $R1 * 10000
  IntOp $R9 $R1 + $R4

  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd

; CheckProjectDbUpgrade: 比较安装包（$PLUGINSDIR\pkg_template_dbver.ini）与已安装
; （$INSTDIR\ProjectInfo\TemplateDataBase）的数据库版本。
; 输出 $R3：0 = 无需升级；1 = 安装包更高或已安装版本缺失。
Function CheckProjectDbUpgrade
  Push $R0
  Push $R1
  Push $R2
  Push $R4
  Push $R5
  Push $R6
  Push $R9

  StrCpy $R3 0
  StrCpy $R4 "ADMIN DESI CATA PROP PADD ISOD COMP CAE"
  dbver_loop:
  ${If} $R4 == ""
    Goto dbver_done
  ${EndIf}
  ${WordFind} "$R4" " " "+1" $R5
  StrLen $R6 $R5
  IntOp $R6 $R6 + 1
  StrCpy $R4 $R4 "" $R6

  ReadINIStr $R0 "$INSTDIR\ProjectInfo\TemplateDataBase\AutoPDMSDBVersion.ini" "PDMSDBVERSION" "$R5"
  ReadINIStr $R1 "$PLUGINSDIR\pkg_template_dbver.ini" "PDMSDBVERSION" "$R5"
  ${If} $R0 == ""
    StrCpy $R3 1
  ${ElseIf} $R1 != ""
    Call DbVersionToScaled
    StrCpy $R6 $R9
    StrCpy $R0 $R1
    Call DbVersionToScaled
    ${If} $R9 > $R6
      StrCpy $R3 1
    ${EndIf}
  ${EndIf}
  Goto dbver_loop

  dbver_done:
  Pop $R9
  Pop $R6
  Pop $R5
  Pop $R4
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd


; #############################################################################
; #############################################################################
; # 安装脚本部分 - 主程序文件安装
; #############################################################################
Section "MainSection" SEC01
!ifdef DELTA_MODE
  ; ---------------- 增量模式：清单驱动的备份/复制/删除/校验 ----------------
  ; delta_files.nsh 由 New-DeltaUpdate.ps1 按 manifest 差异生成
  !include "${DELTA_INCLUDE}"
!else
  ; 设置安装输出目录
  SetOutPath "$INSTDIR"
  ; 复制主程序根目录下的所有文件
  File /x *.ldb /x *.dwl /x *.bak* /x .gitignore "${PRODUCT_SOURCE_DIR_ROOT}\*.*"

  ; ----------------------------------------------------------------------------- 
  ; AutoCAD 相关文件
  ; ----------------------------------------------------------------------------- 
  ; Release installer: remove stray Debug PHS modules left by a Debug install.
  Delete "$INSTDIR\AutoCAD-2007-WIN32\APHSCommand10d.arx"
  Delete "$INSTDIR\AutoCAD-2007-WIN32\APHSCalBridge10d.dll"
  Delete "$INSTDIR\AutoCAD-2007-WIN32\APPipeCommand10d.arx"
  Delete "$INSTDIR\AutoCAD-2007-WIN32\APMenu10d.arx"
  SetOutPath "$INSTDIR\AutoCAD-2007-WIN32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2007-WIN32\*.*" ; 复制 AutoCAD 2007 32位相关文件

  ; AutoCAD 2010 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2010-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2010-Win32\*.*"

  SetOutPath "$INSTDIR\AutoCAD-2013-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2013-Win32\*.*" ; 复制 AutoCAD 2013 32位相关文件

  ; AutoCAD 2015 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2015-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2015-Win32\*.*"
  
  ; AutoCAD 2017 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2017-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2017-Win32\*.*"
  
  ; AutoCAD 2019 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2019-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2019-Win32\*.*"

  ; AutoCAD 2021 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2021-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2021-Win32\*.*"
  
  ; ----------------------------------------------------------------------------- 
  ; AutoCAD 64位相关文件
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\AutoCAD-2010-x64"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2010-x64\*.*" ; 复制 AutoCAD 2010 64位相关文件

  SetOutPath "$INSTDIR\AutoCAD-2013-x64"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2013-x64\*.*" ; 复制 AutoCAD 2013 64位相关文件

  SetOutPath "$INSTDIR\AutoCAD-2015-x64"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2015-x64\*.*" ; 复制 AutoCAD 2015 64位相关文件

  ; AutoCAD 2017 64位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2017-x64"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2017-x64\*.*"

  ; AutoCAD 2019 64位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2019-x64"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2019-x64\*.*"

  ; AutoCAD 2021 64位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\AutoCAD-2021-x64"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\AutoCAD-2021-x64\*.*"
  ; ----------------------------------------------------------------------------- 
  ; 中望CAD 相关文件
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\ZWCAD-2019-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\ZWCAD-2019-Win32\*.*" ; 复制中望CAD 2019 32位相关文件

  SetOutPath "$INSTDIR\ZWCAD-2020-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\ZWCAD-2020-Win32\*.*" ; 复制中望CAD 2020 32位相关文件
  
  ; 中望CAD 2021 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\ZWCAD-2021-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\ZWCAD-2021-Win32\*.*"

  ; 中望CAD 2022 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\ZWCAD-2022-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\ZWCAD-2022-Win32\*.*"

  ; 中望CAD 2023 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\ZWCAD-2023-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\ZWCAD-2023-Win32\*.*"
  
  ; ----------------------------------------------------------------------------- 
  ; 浩辰CAD 相关文件
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\GstarCAD-2019-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\GstarCAD-2019-Win32\*.*" ; 复制浩辰CAD 2019 32位相关文件

  SetOutPath "$INSTDIR\GstarCAD-2020-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\GstarCAD-2020-Win32\*.*" ; 复制浩辰CAD 2020 32位相关文件

  ; 浩辰CAD 2021 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\GstarCAD-2021-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\GstarCAD-2021-Win32\*.*"

  ; 浩辰CAD 2022 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\GstarCAD-2022-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\GstarCAD-2022-Win32\*.*"
  
  ; 浩辰CAD 2023 32位相关文件已注释，暂不复制
  ;SetOutPath "$INSTDIR\GstarCAD-2023-Win32"
  ;File /r "${PRODUCT_SOURCE_DIR_ROOT}\GstarCAD-2023-Win32\*.*"
  
  ; ----------------------------------------------------------------------------- 
  ; VC 相关文件
  ; ----------------------------------------------------------------------------- 
  ; Release installer: remove the old split bridge copy and Debug PHS modules.
  Delete "$INSTDIR\CPP-2005-Win32\APHSCalBridged.dll"
  Delete "$INSTDIR\CPP-2005-Win32\APHSCalBridge10d.dll"
  Delete "$INSTDIR\CPP-2005-Win32\APHSOrgUI10d.dll"
  SetOutPath "$INSTDIR\CPP-2005-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\CPP-2005-Win32\*.*" ; 复制 VC 2005 32位相关文件

  SetOutPath "$INSTDIR\CPP-2010-Win32"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\CPP-2010-Win32\*.*" ; 复制 VC 2010 32位相关文件

  ; ----------------------------------------------------------------------------- 
  ; 配置文件相关
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\config"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\config\*.*" ; 复制 配置文件相关

  SetOutPath "$INSTDIR\Common Mdb"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\Common Mdb\*.*" ;  复制公共数据库相关

  ; 选型库种子必须随包更新，强制覆盖（工程库 AllPrjDB 仍保持 ifnewer）
  SetOverwrite on
  SetOutPath "$INSTDIR\PHSPrjDB"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\PHSPrjDB\*.*" ; 复制支吊架选型库正本（deploy\phs\db）
  SetOverwrite ifnewer

  SetOutPath "$INSTDIR\PHSBlock"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\PHSBlock\*.*" ; 复制支吊架零件块库（deploy\phs\block：phs + opx）

  SetOutPath "$INSTDIR\PHS\Config"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\PHS\Config\*.*" ; 复制支吊架出图配置（deploy\phs\config：ini/布局/表格/图框）

  ; ----------------------------------------------------------------------------- 
  ; AI MCP Server（tools\mcp + 内置 Python 运行时，客户机无需安装 Python）
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\AI\MCP"
  File /r /x *.ldb /x *.dwl /x *.bak* /x __pycache__ "${PRODUCT_SOURCE_DIR_ROOT}\AI\MCP\*.*" ; 复制 MCP Server 与内置 Python（deploy\ai）

  ; ----------------------------------------------------------------------------- 
  ; 项目文件相关（条件编译）
  ; ----------------------------------------------------------------------------- 
  ; 覆盖安装策略（在线更新会装进同一目录，不能动用户工程）：
  ;   1) $INSTDIR\project 不存在：全量安装；
  ;   2) 已存在，且安装包数据库版本不高于已安装版本：整段跳过；
  ;   3) 已存在，但安装包数据库版本更高：只覆盖数据库文件（*.mdb / AutoPDMSDBVersion.ini）。
  ; 注意：SetOutPath 会创建目录，之后 *.* 恒存在，因此必须在它之前判断是否首次安装。
  !ifdef TAG_SUPPORT_PROJECT_FILES
    ${If} ${FileExists} "$INSTDIR\project\*.*"
      StrCpy $R8 1
    ${Else}
      StrCpy $R8 0
    ${EndIf}
  !endif
  SetOutPath "$INSTDIR\project"
  ; 只有定义了 TAG_SUPPORT_PROJECT_FILES 才复制项目文件，避免安装包超过2GB限制 ligb on 2022-08-13
  !ifdef TAG_SUPPORT_PROJECT_FILES
    ${If} $R8 == 0
      DetailPrint "首次安装：写入 project 目录"
      SetOverwrite on
      File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\project\*.*" ; 复制项目文件TAG_SUPPORT_PROJECT_FILES已定义才复制
      SetOverwrite ifnewer
    ${Else}
      InitPluginsDir
      File /nonfatal "/oname=$PLUGINSDIR\pkg_template_dbver.ini" "${PRODUCT_SOURCE_DIR_ROOT}\ProjectInfo\TemplateDataBase\AutoPDMSDBVersion.ini"
      Call CheckProjectDbUpgrade
      ${If} $R3 == 1
        DetailPrint "数据库版本升级：只更新 project 下的数据库文件"
        SetOverwrite on
        File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\project\*.mdb"
        File /r "${PRODUCT_SOURCE_DIR_ROOT}\project\AutoPDMSDBVersion.ini"
        SetOverwrite ifnewer
      ${Else}
        DetailPrint "project 目录已存在且数据库版本未变，跳过覆盖"
      ${EndIf}
    ${EndIf}
  !else
    !echo "不复制project文件！" ; 提示不复制项目文件
  !endif

  SetOutPath "$INSTDIR\projectinfo"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\projectinfo\*.*" ; 复制项目信息相关

  SetOutPath "$INSTDIR\help"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\help\*.*" ; 复制 帮助 文件

  ; ----------------------------------------------------------------------------- 
  ; 插件相关文件
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\add-in"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\add-in\*.*" ; 复制 插件相关

  SetOutPath "$INSTDIR\ccx"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\ccx\*.*" ; 复制 ccx 插件相关

  SetOutPath "$INSTDIR\import"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\import\*.*" ; 复制 导入插件相关
  
  ; ----------------------------------------------------------------------------- 
  ; WaterPro水利软件相关文件
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\${SUB_PRODUCT_NAME_Material}\${SUB_PRODUCT_NAME_WATERPRO}"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\${SUB_PRODUCT_NAME_Material}\${SUB_PRODUCT_NAME_WATERPRO}\*.*" ; 复制水利软件相关文件

  ; ----------------------------------------------------------------------------- 
  ; 第三方即时通讯客户端 Pidgin（协同/GLOBAL，x64，含 libprpl-ueadmin 协议插件）
  ; 该目录自带 libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll 等与主程序
  ; 同名不同版本的运行库，必须整体装进独立子目录，不能平铺进 $INSTDIR 根目录。
  ; 服务端接入信息（pidgin.uesoft.com / im.uesoft.com）在 config\config.ini 的
  ; [Internet] 节里，随上面的 config 复制规则一并安装，此处无需重复写入。
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR\${SUB_PRODUCT_PATH_PIDGIN}"
  File /r /x *.ldb /x *.dwl /x *.bak* "${PRODUCT_SOURCE_DIR_ROOT}\${SUB_PRODUCT_SRC_DIR_PIDGIN}\*.*" ; 复制 Pidgin 客户端全部文件

  ; ----------------------------------------------------------------------------- 
  ; MinGW相关运行库（已注释）
  ; ----------------------------------------------------------------------------- 
  ; 2022-07-08 注释掉CodeBlocks 编译器相关DLL，因为Windows 10系统自带这些库
  ;File "${PRODUCT_SOURCE_DIR_ROOT}\libgcc_s_dw2-1.dll" ; GCC异常处理DW2相关库
  ;File "${PRODUCT_SOURCE_DIR_ROOT}\libgcc_s_seh-1.dll" ; GCC异常处理SEH相关库
  ;File "${PRODUCT_SOURCE_DIR_ROOT}\libgcc_s_sjlj-1.dll" ; GCC异常处理SJLJ相关库
  ; 2022-07-08 注释掉以下MinGW相关DLL
  ;File "${PRODUCT_SOURCE_DIR_ROOT}\libwinpthread-1.dll" ; WinPthread 线程库
  ;File "${PRODUCT_SOURCE_DIR_ROOT}\libstdc++-6.dll" ; C++ 标准库

  ; ----------------------------------------------------------------------------- 
  ; VC运行库检测与安装
  ; ----------------------------------------------------------------------------- 
  SetOutPath "$INSTDIR"
  
  ; 机器级"本机只装一次"标记：只要写过就不再自动运行合集安装程序，
  ; 卸载/重装 AutoPDMS 也不重跑（卸载段不删此键，因为运行库本身也不删）。
  StrCpy $R6 0
  ClearErrors
  ReadRegDWORD $R6 HKLM "${PRODUCT_REG_DIR_VCRUNTIME}" "Installed"
  
  ${If} $R6 == 1
    DetailPrint "本机此前已安装过 VC 运行库合集，跳过（只安装一次）"
  ${Else}
  
  ; ----------------------------------------------------------------------------- 
  ; 检测VC运行库是否已安装
  ; ----------------------------------------------------------------------------- 
  ; 初始化标志变量，1表示需要安装，0表示不需要安装
  StrCpy $R0 1
  
  ; 检测VC运行库是否已安装，如果已安装则跳过安装过程
  ; 优先检测VC 2015-2022运行库是否已安装，该版本包含VCRUNTIME140.dll
  
  ; 检测VC 2015-2022运行库是否已安装32位
  ; 检测VC 2015-2017-2019-2022运行库是否已安装（Visual Studio 14.0对应VC 2015）
  ReadRegStr $R1 HKLM "SOFTWARE\Wow6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x86" "Version" 
  ${If} $R1 == "" ; 64位系统下32位程序运行库路径，32位系统下直接查询
    ReadRegStr $R1 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x86" "Version" 
  ${EndIf} 
  
  ${If} $R1 != "" ; 已安装VC 2015-2022运行库
    ; 检测VC 2013运行库是否已安装32位
    ReadRegStr $R2 HKLM "SOFTWARE\Wow6432Node\Microsoft\VisualStudio\12.0\VC\Runtimes\x86" "Version" 
    ${If} $R2 == "" 
      ReadRegStr $R2 HKLM "SOFTWARE\Microsoft\VisualStudio\12.0\VC\Runtimes\x86" "Version" 
    ${EndIf} 
    
    ${If} $R2 != "" ; 已安装VC 2013运行库
      ; 检测VC 2010运行库是否已安装32位
      ReadRegStr $R3 HKLM "SOFTWARE\Wow6432Node\Microsoft\VisualStudio\10.0\VC\Redist\x86\{196BB40D-1578-3D01-B289-BEFC77A11A1E}" "Version" 
      ${If} $R3 == "" 
        ReadRegStr $R3 HKLM "SOFTWARE\Microsoft\VisualStudio\10.0\VC\Redist\x86\{196BB40D-1578-3D01-B289-BEFC77A11A1E}" "Version" 
      ${EndIf} 
      
      ${If} $R3 != "" ; 已安装VC 2010运行库
        ; 检测VC 2008运行库是否已安装32位
        ReadRegStr $R4 HKLM "SOFTWARE\Wow6432Node\Microsoft\VisualStudio\9.0\VC\Redist\x86\{FF66E9F6-83E7-3A3E-AF14-8DE9A809A6A4}" "Version" 
        ${If} $R4 == "" 
          ReadRegStr $R4 HKLM "SOFTWARE\Microsoft\VisualStudio\9.0\VC\Redist\x86\{FF66E9F6-83E7-3A3E-AF14-8DE9A809A6A4}" "Version" 
        ${EndIf} 
        
        ${If} $R4 != "" ; 已安装VC 2008运行库
          ; 检测VC 2005运行库是否已安装32位
          ReadRegStr $R5 HKLM "SOFTWARE\Wow6432Node\Microsoft\VisualStudio\8.0\VC\Redist\x86\{A49F249F-0C91-497F-86DF-B2585E8E76B7}" "Version" 
          ${If} $R5 == "" 
            ReadRegStr $R5 HKLM "SOFTWARE\Microsoft\VisualStudio\8.0\VC\Redist\x86\{A49F249F-0C91-497F-86DF-B2585E8E76B7}" "Version" 
          ${EndIf} 
          
          ${If} $R5 != "" ; 已安装VC 2005运行库
            ; 所有VC运行库都已安装，不需要再安装
            StrCpy $R0 0
          ${EndIf} 
        ${EndIf} 
      ${EndIf} 
    ${EndIf} 
  ${EndIf} 
  
  ; 根据检测结果决定是否安装VC运行库
  ${If} $R0 == 1
    ; 只有确实需要安装时才把合集安装程序（约104MB）释放到磁盘，已装过的机器不再落这份拷贝
    File "${VCCollFN}" ; 复制VC运行库集成安装程序
    DetailPrint "正在安装 VC 运行库合集（本机首次，静默模式）..."
    ; 执行VC运行库安装程序，使用静默安装模式，不显示界面
    ; 可用参数包括：/silent /verysilent /quiet /norestart
    ClearErrors
    ExecWait "$INSTDIR\${VCCollReFileName} /silent /norestart" $0
    ; 0=成功；3010=成功但需重启后生效；1641=已发起重启，三者都算"本机装过"；
    ; 其余返回码不写标记，下次安装会重试（避免安装失败却被永久跳过）
    ${If} $0 == 0
    ${OrIf} $0 == 3010
    ${OrIf} $0 == 1641
      WriteRegDWORD HKLM "${PRODUCT_REG_DIR_VCRUNTIME}" "Installed" 1
      WriteRegStr HKLM "${PRODUCT_REG_DIR_VCRUNTIME}" "LastRunApp" "${VCCollReFileName}"
      DetailPrint "VC 运行库合集安装完成（返回码 $0），已标记本机只装一次"
    ${Else}
      DetailPrint "VC 运行库合集安装返回码 $0，未标记只装一次，下次安装将重试"
    ${EndIf}
  ${Else}
    ; 不需要安装VC运行库，显示提示信息
    DetailPrint "检测到VC运行库已安装，跳过VC运行库安装"
  ${EndIf}
  ${EndIf}

!endif ; DELTA_MODE
SectionEnd

; #############################################################################
; # MinGW编译器相关（已注释）
; #############################################################################
Section "${MINGW_NAME_VERSION}" SEC02
!ifndef DELTA_MODE
  /*  ; 安装MinGW编译器（已注释）
    SetOutPath "$INSTDIR\${MINGW_DIR}"
    SetOverwrite ifnewer ; 仅当文件较新时覆盖
    File /r "${PRODUCT_MINGW_SOURCE_DIR}\*.*" ; 复制MinGW编译器相关文件
  */
!endif ; DELTA_MODE
SectionEnd

; #############################################################################
; # 创建快捷方式和URL链接
; #############################################################################
Section -AdditionalIcons
  ; 设置输出目录
  SetOutPath $INSTDIR
  
  ; 创建网站URL快捷方式
  WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_PUBLISHER_WEB}"
  
  ; 创建网站URL快捷方式
  CreateShortCut "$SMPROGRAMS\${AP_INSDIR}\Website.lnk" "${PRODUCT_WEB_SITE}.url" ; 网站快捷方式
  CreateShortCut "$SMPROGRAMS\${AP_INSDIR}\Uninstall.lnk" "$INSTDIR\uninst.exe" ; 卸载程序快捷方式

  ; 配置 AI MCP 客户端快捷方式（使用随包内置的 Python，不需要系统 Python）
  CreateShortCut "$SMPROGRAMS\${AP_INSDIR}\配置 AI MCP.lnk" "$INSTDIR\AI\MCP\install_mcp.cmd" "" "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}" 0
  
  ; 复制主程序文件（差量模式由清单按需覆盖，避免重复嵌入）
!ifndef DELTA_MODE
  File "${PRODUCT_SOURCE_DIR_ROOT}\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}"
!endif
  
  ; 创建程序组目录
  CreateDirectory "$SMPROGRAMS\${APPDMSLOGIN_INSDIR}"
  
  ; 创建开始菜单程序快捷方式
  CreateShortCut "$SMPROGRAMS\${AP_INSDIR}.lnk" "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}"
  
  ; 创建桌面快捷方式
  ; 快捷方式不再带版本号；升级安装时先删掉旧版本遗留的桌面快捷方式
  ; （形如“UESoft AutoPDMS 11.7.124”），避免桌面残留多个版本
  Delete "$DESKTOP\${PRODUCT_NAME} *.lnk"
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}" ; 主程序桌面快捷方式
  
  ; 创建WaterPro水利软件快捷方式
  CreateShortCut "$DESKTOP\${PRODUCT_NAME_WATERPRO_EXE}.lnk" "$INSTDIR\${SUB_PRODUCT_PATH_WATERPRO_EXE_DIR}\${SUB_PRODUCT_NAME_WATERPRO_EXE}"
SectionEnd


; #############################################################################
; # 注册表配置
; #############################################################################
Section -Post
  ; 安装成功：清掉增量失败回落标记（全量安装也视为修复完成）
  Delete "$LOCALAPPDATA\AutoPDMS\update\fallback.json"
  ; 生成卸载程序
  WriteUninstaller "$INSTDIR\uninst.exe"
  
  ; 写入程序安装路径注册表项
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}"
  
  ; 写入Path环境变量
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "Path" "$INSTDIR;$INSTDIR\${SUB_DIR_DLL};$INSTDIR\${SUB_DIR_XRX}"
  ;WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "Path" "$INSTDIR;$INSTDIR\${SUB_DIR_DLL_ZW};$INSTDIR\${SUB_DIR_XRX_ZW}" ; ZWCAD相关路径
  
  ; 写入AutoPDMS相关注册表项
  WriteRegStr HKLM "${PRODUCT_REG_DIR}" "ArxIniPath" "$INSTDIR" ; ARX配置文件路径
  WriteRegStr HKLM "${PRODUCT_REG_DIR_AUTOPDMS}" "CurrentVersion" "${PRODUCT_VERSION}" ; 当前版本号
  WriteRegStr HKLM "${PRODUCT_REG_DIR_AUTOPDMS}" "ArxIniPath" "$INSTDIR" ; 通用键也记安装位置（旧客户端 GetInstallDir 读它）
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR" ; 供下次安装/更新自动沿用目录

  ; 自动把 AI MCP 写入本机已安装的 MCP 客户端（Qoder CN / WorkBuddy / Claude Desktop / Cursor / Claude Code）
  ; 客户端稍后才安装时，可用开始菜单“配置 AI MCP”重新执行
  ${If} ${FileExists} "$INSTDIR\AI\MCP\python\python.exe"
    DetailPrint "正在配置 AI MCP 客户端..."
    nsExec::ExecToLog '"$INSTDIR\AI\MCP\python\python.exe" "$INSTDIR\AI\MCP\install_mcp.py" --auto --quiet'
    Pop $0
  ${EndIf}
  
  ; 静默安装（客户端在线更新）：装完自动拉起登录器，否则用户看到的是"程序自己消失、没有重新出现"。
  ; 非静默安装（首次安装/手动运行安装包）由 MUI 完成页的 RUN 负责。
  ${If} ${Silent}
    DetailPrint "静默安装完成，正在启动 ${PRODUCT_MAIN_APP_NAME_EXT} ..."
    Sleep 800
    Exec '"$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}"'
  ${EndIf}
  
  ; 写入卸载相关注册表项
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name)" ; 程序名称
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninst.exe" ; 卸载命令
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}" ; 程序图标
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}" ; 显示版本
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}" ; 网站信息
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER_WEB}" ; 发布者
  
  ; 设置程序以管理员权限运行
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\CPP-2005-WIN32\${PRODUCT_MAIN_APP_NAME_EXT}" "RUNASADMIN"
  
  ; 添加MinGW相关DLL到系统PATH（如果使用CodeBlocks for windows编译器，需要将以下DLL路径添加到系统PATH）
  ; libgcc_s_dw2-1.dll, libwinpthread-1.dll, libstdc++-6.dll
  ; 注意：以下代码仅在MinGW编译环境下启用，用于将MinGW相关DLL路径添加到系统PATH，确保程序能够正常运行
  ;ReadRegStr $0 HKLM "system\CurrentControlSet\Control\Session Manager\Environment" "Path"
  ;WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0;$INSTDIR\MINGW"
  
  ; 使用WordReplace宏更新PATH环境变量，移除旧的AutoPDMS路径
  ReadRegStr $R0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  ${WordReplace} $R0 ";$INSTDIR;$INSTDIR\${SUB_DIR_DLL};$INSTDIR\${SUB_DIR_XRX}" "" "+" $R1
  ;MessageBox MB_OK|MB_USERICON '$R0 - $INSTDIR - $R1 ' ; 调试信息显示PATH变量
  WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$R1"

  ; ------------------------------------------------------------------------- 
  ; 注册 UELoginReturn URL Protocol
  ; ------------------------------------------------------------------------- 
  DetailPrint "正在注册 UELoginReturn URL Protocol..." 
  WriteRegStr HKCR "UELoginReturn" "" "" 
  WriteRegStr HKCR "UELoginReturn" "URL Protocol" "用于浏览器回调通知UE程序告知操作已完成" 
  WriteRegStr HKCR "UELoginReturn\shell\open\command" "" '$\"$INSTDIR\CPP-2005-Win32\ThirdPartyLoginReturn10.exe$\" $\"%1$\"' 

  ; ------------------------------------------------------------------------- 
  ; 设置 APPDMSLogin10.exe 的 DPI 缩放行为为“系统(增强)”
  ; ------------------------------------------------------------------------- 
  DetailPrint "设置主程序 APPDMSLogin10.exe 的替代 DPI 缩放行为..." 
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\APPDMSLogin10.exe" "~ GDIDPISCALING DPIUNAWARE"
  WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\CPP-2005-Win32\APPDMSLogin10.exe" "~ GDIDPISCALING DPIUNAWARE"

  ; ------------------------------------------------------------------------- 
  ; 生成支吊架计算配置 APHSCal.ini（bridge DLL 同目录，路径随安装机自适应）
  ; 键集与 deploy\phs\setup_phs_env.bat 的生产模板一致；代码首启按
  ; GetModuleFileName(APHSCalBridge10.dll) 同目录查找此文件（HSTrCfgPath.cpp）。
  ; 升级安装时 WriteINIStr 会刷新键值。
  ; ------------------------------------------------------------------------- 
  DetailPrint "生成支吊架计算配置 APHSCal.ini..." 
  ; 写两份：主副本在 bridge DLL 同目录（AutoCAD-2007-WIN32，RegisterHSCommand 按
  ; GetModuleFileName(ARX) 同目录加载 APHSCalBridge10.dll 并定位 ini）；
  ; 副本在 CPP-2010-Win32（HSTrCfgPath 桥未加载时回退 APHSTransaction10.dll 同目录）。
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "ShareDir" "$INSTDIR\Common Mdb\"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "ProjectDBDir" "$INSTDIR\PHSPrjDB\"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "ProjectDir" "$LOCALAPPDATA\AutoPDMS\PHS\work\"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "TemplateDir" "$INSTDIR\PHSPrjDB\"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "BlockDir" "$INSTDIR\PHSBlock\"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "BomOutDir" "$LOCALAPPDATA\AutoPDMS\PHS\out\"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "VolumeID" ""
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "Zdjh" ""
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "ZdjhSource" "RestID"
  WriteINIStr "$INSTDIR\AutoCAD-2007-WIN32\APHSCal.ini" "PhsCal" "SumAllVolume" "0"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "ShareDir" "$INSTDIR\Common Mdb\"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "ProjectDBDir" "$INSTDIR\PHSPrjDB\"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "ProjectDir" "$LOCALAPPDATA\AutoPDMS\PHS\work\"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "TemplateDir" "$INSTDIR\PHSPrjDB\"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "BlockDir" "$INSTDIR\PHSBlock\"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "BomOutDir" "$LOCALAPPDATA\AutoPDMS\PHS\out\"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "VolumeID" ""
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "Zdjh" ""
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "ZdjhSource" "RestID"
  WriteINIStr "$INSTDIR\CPP-2010-Win32\APHSCal.ini" "PhsCal" "SumAllVolume" "0"

  ; WorkPrj.mdb is the engine's scratch DB. Legacy Jet 3 files make the
  ; Jet 4 DAO report a bogus 3031 on every connect variant; seed the
  ; per-user work DB from the packaged Jet 4 template (keeping the old file).
  CreateDirectory "$LOCALAPPDATA\AutoPDMS\PHS\work"
  IfFileExists "$LOCALAPPDATA\AutoPDMS\PHS\work\WorkPrj.mdb" 0 workprj_seed
    Delete "$LOCALAPPDATA\AutoPDMS\PHS\work\WorkPrj.mdb.bak-${PRODUCT_VERSION}"
    Rename "$LOCALAPPDATA\AutoPDMS\PHS\work\WorkPrj.mdb" "$LOCALAPPDATA\AutoPDMS\PHS\work\WorkPrj.mdb.bak-${PRODUCT_VERSION}"
    Delete "$LOCALAPPDATA\AutoPDMS\PHS\work\WorkPrj.ldb"
    Delete "$LOCALAPPDATA\AutoPDMS\PHS\work\phs_scratch.mdb"
    Delete "$LOCALAPPDATA\AutoPDMS\PHS\work\phs_scratch.ldb"
  workprj_seed:
  CopyFiles /SILENT "$INSTDIR\PHSPrjDB\WORKPRJ.MDB" "$LOCALAPPDATA\AutoPDMS\PHS\work\WorkPrj.mdb"

SectionEnd
; #############################################################################
; # 统计安装目录实际体积并写入注册表 EstimatedSize
; # 原实现（ligb on 2022-02-15）读取正在写出的安装包，得到的是压缩后大小
; #############################################################################
Section EstimateInstallSize
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2  ; $0=KB, $1=文件数, $2=目录数
  ${If} $0 != "error"
    WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"
  ${EndIf}
SectionEnd


; #############################################################################
; # 安装程序初始化函数
; #############################################################################
Function .onInit
!ifdef DELTA_MODE
  ; 增量包只由客户端在线更新以 /S 调起；手动双击会装到错误目录
  IfSilent delta_silent_ok
    MessageBox MB_ICONSTOP "增量更新包不能手动安装，请在 AutoPDMS 客户端中检查更新。"
    Quit
  delta_silent_ok:
  ; 基线必须精确等于打包时的 from_version，否则拒绝安装并留回落标记
  ReadRegStr $0 HKLM "${PRODUCT_REG_DIR_AUTOPDMS}" "CurrentVersion"
  StrCmp $0 "${DELTA_FROM_VERSION}" delta_baseline_ok
    StrCpy $R9 "baseline-mismatch"
    Call DeltaWriteFallback
    DetailPrint "增量包基线不匹配（当前 $0，需要 ${DELTA_FROM_VERSION}），已中止"
    SetErrorLevel 2
    Quit
  delta_baseline_ok:
!endif
  ;!insertmacro MULTIUSER_INIT ; 多用户安装初始化（当前未启用）

  ; 客户端在线更新以 /S 启动安装程序后立即退出自身；稍等它释放安装目录里的 DLL，
  ; 避免复制文件时撞上文件占用（静默安装没有"重试"按钮，失败即中止）。
  ${If} ${Silent}
    Sleep 1200
  ${EndIf}

  ; 1) 命令行带 /D=<目录>（客户端在线更新）时，NSIS 已写入 $INSTDIR，直接采用（目录页只读）
  ${GetOptions} $CMDLINE "/D=" $0
  ${IfNot} ${Errors}
    StrCpy $PreviousInstallDir $INSTDIR
    Return
  ${EndIf}

  ; 2) 已安装过：通用键 CurrentVersion -> 带版本键里的 ArxIniPath（安装时写入）
  ReadRegStr $0 HKLM "${PRODUCT_REG_DIR_AUTOPDMS}" "CurrentVersion"
  StrCpy $1 ""
  ${If} $0 != ""
    ReadRegStr $1 HKLM "${PRODUCT_REG_DIR_AUTOPDMS}$0" "ArxIniPath"
  ${EndIf}

  ; 3) 兜底：卸载项的 InstallLocation / 主程序路径注册表项
  ${If} $1 == ""
    ReadRegStr $1 HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation"
  ${EndIf}
  ${If} $1 == ""
    ReadRegStr $1 HKLM "${PRODUCT_DIR_REGKEY}" ""
    ${If} $1 != ""
      ${GetParent} "$1" $1
      ${GetParent} "$1" $1
    ${EndIf}
  ${EndIf}

  ; 目录仍存在才沿用（残留注册表不把用户带到空目录），否则按首次安装走默认目录
  ${If} $1 != ""
    ${If} ${FileExists} "$1\CPP-2005-WIN32\*.*"
      StrCpy $INSTDIR $1
      StrCpy $PreviousInstallDir $1
    ${EndIf}
  ${EndIf}
FunctionEnd

; 已安装过：目录页仍显示（让用户看到装到哪），但路径框与“浏览”按钮禁用
!ifdef DELTA_MODE
; $R9 = 失败原因；写 %LOCALAPPDATA%\AutoPDMS\update\fallback.json 供登录器回落全量
Function DeltaWriteFallback
  CreateDirectory "$LOCALAPPDATA\AutoPDMS\update"
  FileOpen $9 "$LOCALAPPDATA\AutoPDMS\update\fallback.json" w
  FileWrite $9 '{"fromVersion":"${DELTA_FROM_VERSION}","toVersion":"${PRODUCT_VERSION}","reason":'
  FileWrite $9 '"'
  FileWrite $9 $R9
  FileWrite $9 '"}'
  FileClose $9
FunctionEnd
!endif ; DELTA_MODE

Function DirectoryPageShow
  ${If} $PreviousInstallDir == ""
    Return
  ${EndIf}

  FindWindow $0 "#32770" "" $HWNDPARENT
  GetDlgItem $1 $0 1019          ; 安装路径输入框
  ${If} $1 != 0
    EnableWindow $1 0
  ${EndIf}
  GetDlgItem $1 $0 1001          ; “浏览...”按钮
  ${If} $1 != 0
    EnableWindow $1 0
  ${EndIf}
  GetDlgItem $1 $0 1006          ; 页面顶部说明文字
  ${If} $1 != 0
    SendMessage $1 ${WM_SETTEXT} 0 "STR:检测到已安装的 AutoPDMS，将沿用原来的安装目录（本页路径不可修改）。"
  ${EndIf}
FunctionEnd


; #############################################################################
; # 卸载成功回调函数
; #############################################################################
Function un.onUninstSuccess
  HideWindow                  ; 隐藏卸载窗口
  MessageBox MB_ICONINFORMATION|MB_OK "$(^Name) 已成功地从你的计算机卸载。" ; 显示卸载成功提示
FunctionEnd

; #############################################################################
; # 卸载程序初始化函数
; #############################################################################
Function un.onInit
  ; 显示卸载确认对话框，询问用户是否确定要卸载程序
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "你确定要完全移除 $(^Name) 及其所有组件吗？" IDYES +2
  Abort                      ; 如果用户选择否，则中止卸载过程
FunctionEnd


; #############################################################################
; # 卸载程序 - 删除所有安装的文件和注册表项
; #############################################################################
Section Uninstall
        ; 删除快捷方式
        Delete "$SMPROGRAMS\${AP_INSDIR}\Uninstall.lnk"
        Delete "$SMPROGRAMS\${AP_INSDIR}\配置 AI MCP.lnk"
        IfErrors 0 +2
        
        Delete "$SMPROGRAMS\${AP_INSDIR}\Website.lnk"
        IfErrors 0 +2
        
        Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
        IfErrors 0 +2
        
        ; 兼容更早版本遗留的带版本号快捷方式（如“UESoft AutoPDMS 11.7.124”）
        Delete "$DESKTOP\${PRODUCT_NAME} *.lnk"
        IfErrors 0 +2
        
        Delete "$DESKTOP\${SUB_PRODUCT_PATH_WATERPRO_DESKTOP}.lnk"
        IfErrors 0 +2
        
        Delete "$SMPROGRAMS\${AP_INSDIR}.lnk"
        IfErrors 0 +2
        
        ; 删除程序组目录
        RMDir "$SMPROGRAMS\${AP_INSDIR}"
        IfErrors 0 +2
        
        ; 删除URL快捷方式
        Delete "$INSTDIR\${PRODUCT_NAME_VERSION}.url"
        IfErrors 0 +2
        
        ; 删除程序目录 - ACAD
        RMDir /r "$INSTDIR\AutoCAD-2007-Win32"
        RMDir /r "$INSTDIR\AutoCAD-2010-x64"
        RMDir /r "$INSTDIR\AutoCAD-2013-Win32"
        RMDir /r "$INSTDIR\AutoCAD-2013-x64"
        RMDir /r "$INSTDIR\AutoCAD-2015-Win32"
        RMDir /r "$INSTDIR\AutoCAD-2015-x64"
        RMDir /r "$INSTDIR\AutoCAD-2017-Win32"
        RMDir /r "$INSTDIR\AutoCAD-2017-x64"
        RMDir /r "$INSTDIR\AutoCAD-2019-Win32"
        RMDir /r "$INSTDIR\AutoCAD-2019-x64"
        RMDir /r "$INSTDIR\AutoCAD-2021-Win32"
        RMDir /r "$INSTDIR\AutoCAD-2021-x64"
        
        ; 删除程序目录 - 中望CAD
        RMDir /r "$INSTDIR\ZWCAD-2019-Win32"
        RMDir /r "$INSTDIR\ZWCAD-2020-Win32"
        RMDir /r "$INSTDIR\ZWCAD-2021-Win32"
        RMDir /r "$INSTDIR\ZWCAD-2022-Win32"
        RMDir /r "$INSTDIR\ZWCAD-2023-Win32"
        
        ; 删除程序目录 - 浩辰CAD
        RMDir /r "$INSTDIR\GstarCAD-2019-Win32"
        RMDir /r "$INSTDIR\GstarCAD-2020-Win32"
        RMDir /r "$INSTDIR\GstarCAD-2021-Win32"
        RMDir /r "$INSTDIR\GstarCAD-2022-Win32"
        RMDir /r "$INSTDIR\GstarCAD-2023-Win32"
        
        ; 删除程序目录 - VC运行库
        RMDir /r "$INSTDIR\CPP-2005-Win32"
        RMDir /r "$INSTDIR\CPP-2010-Win32"
        
        ; 删除程序目录 - 公共文件
        RMDir /r "$INSTDIR\Common Mdb"
        ;RMDir /r "$INSTDIR\config" ; 保留配置文件
        ;RMDir /r "$INSTDIR\project" ; 保留项目文件
        ;RMDir /r "$INSTDIR\projectinfo" ; 保留项目信息
        RMDir /r "$INSTDIR\help"
        RMDir /r "$INSTDIR\add-in"
        RMDir /r "$INSTDIR\ccx"
        RMDir /r "$INSTDIR\import"
        
        ; 删除程序目录 - 水利软件相关
        RMDir /r "$INSTDIR\${SUB_PRODUCT_NAME_Material}\${SUB_PRODUCT_NAME_WATERPRO}"
        
        ; 删除程序目录 - Pidgin 即时通讯客户端
        RMDir /r "$INSTDIR\${SUB_PRODUCT_PATH_PIDGIN}"
        
        ; 删除注册表项
        DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
        IfErrors 0 +2
        
        DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
        IfErrors 0 +2
        
        DeleteRegKey HKLM "${PRODUCT_REG_DIR}"
        IfErrors 0 +2
        
        DeleteRegKey HKLM "${PRODUCT_REG_DIR_AUTOPDMS}"
        IfErrors 0 +2
        
        ; 删除 UELoginReturn URL Protocol 注册表项
        DetailPrint "正在清理 UELoginReturn 注册表项..." 
        DeleteRegKey HKCR "UELoginReturn"
        
        ; 删除 DPI 缩放行为注册表项
        DetailPrint "正在清理 DPI 缩放行为注册表项..." 
        DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\APPDMSLogin10.exe"
        DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\CPP-2005-Win32\APPDMSLogin10.exe"
        
        ; 从系统PATH环境变量中移除AutoPDMS相关路径
        ReadRegStr $R0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
        ${WordReplace} $R0 ";$INSTDIR;$INSTDIR\${SUB_DIR_DLL};$INSTDIR\${SUB_DIR_XRX}" "" "+" $R1
        WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$R1"
        IfErrors 0 +2
        
        ; 删除VC运行库安装程序（如果存在）
        Delete "$INSTDIR\${VCCollReFileName}"

        ; 清理支吊架(PHS)数据与注册表（随"完全移除"一并删除）
        ; 移除指向本安装目录的 AI MCP 客户端配置（只删本安装写入的条目）
        ${If} ${FileExists} "$INSTDIR\AI\MCP\python\python.exe"
          DetailPrint "正在移除 AI MCP 客户端配置..."
          nsExec::ExecToLog '"$INSTDIR\AI\MCP\python\python.exe" "$INSTDIR\AI\MCP\install_mcp.py" --remove --quiet'
          Pop $0
        ${EndIf}

        DetailPrint "正在清理支吊架(PHS)数据目录..."
        RMDir /r "$INSTDIR\PHSPrjDB"
        RMDir /r "$INSTDIR\PHSBlock"
        RMDir /r "$INSTDIR\PHS"
        RMDir /r "$INSTDIR\AI"
        RMDir /r "$LOCALAPPDATA\AutoPDMS\PHS"
        DeleteRegKey HKCU "Software\长沙优易软件开发有限公司\AutoPHS"

        ; 删除卸载程序
        Delete "$INSTDIR\uninst.exe"
        IfErrors 0 +2
        
        ; 删除安装目录（递归清理所有残留，含 config/project 工程数据，与卸载确认框"完全移除"一致）
        RMDir /r "$INSTDIR"
        IfErrors 0 +2
        
        SetAutoClose true
SectionEnd












