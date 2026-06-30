#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UEAdminAPI {
namespace SQLite {

/**
 * @brief SQLite 列值的统一表示, 用 tag + 多字段方式存储, 兼容 SQLite 五种基本类型.
 *
 * 设计原则:
 *  - 对外只暴露不可变接口, 通过工厂函数构造;
 *  - text/blob 拷贝持有, 调用方析构后值仍有效, 简化生命周期;
 *  - 数值类型零成本访问, 避免每次访问都 std::move.
 */
class SqliteValue {
public:
    enum Type {
        vtNull = 0,
        vtInt,
        vtReal,
        vtText,
        vtBlob
    };

    SqliteValue() : _type(vtNull), _intVal(0), _realVal(0.0) {}

    static SqliteValue FromNull() {
        return SqliteValue();
    }

    static SqliteValue FromInt(int64_t v) {
        SqliteValue r;
        r._type = vtInt;
        r._intVal = v;
        return r;
    }

    static SqliteValue FromBool(bool v) {
        return FromInt(v ? 1 : 0);
    }

    static SqliteValue FromReal(double v) {
        SqliteValue r;
        r._type = vtReal;
        r._realVal = v;
        return r;
    }

    static SqliteValue FromText(const std::string& v) {
        SqliteValue r;
        r._type = vtText;
        r._textVal = v;
        return r;
    }

    static SqliteValue FromText(std::string&& v) {
        SqliteValue r;
        r._type = vtText;
        r._textVal = std::move(v);
        return r;
    }

    static SqliteValue FromBlob(const std::vector<uint8_t>& v) {
        SqliteValue r;
        r._type = vtBlob;
        r._blobVal = v;
        return r;
    }

    static SqliteValue FromBlob(std::vector<uint8_t>&& v) {
        SqliteValue r;
        r._type = vtBlob;
        r._blobVal = std::move(v);
        return r;
    }

    Type type() const { return _type; }

    bool isNull() const { return _type == vtNull; }

    // 数值转换: 允许 int 与 real 间的隐式转换, 文本会尝试 stoi/stod, 失败返回 0
    int64_t asInt() const {
        switch (_type) {
        case vtInt:
            return _intVal;
        case vtReal:
            return static_cast<int64_t>(_realVal);
        case vtText:
            try { return std::stoll(_textVal); } catch (...) { return 0; }
        default:
            return 0;
        }
    }

    double asReal() const {
        switch (_type) {
        case vtInt:
            return static_cast<double>(_intVal);
        case vtReal:
            return _realVal;
        case vtText:
            try { return std::stod(_textVal); } catch (...) { return 0.0; }
        default:
            return 0.0;
        }
    }

    // text 总是返回引用; 数值类型走 toString 临时构造, 调用方需自行拷贝
    const std::string& asText() const {
        return _textVal;
    }

    std::string toString() const {
        switch (_type) {
        case vtNull:
            return std::string();
        case vtInt:
            return std::to_string(_intVal);
        case vtReal:
            return std::to_string(_realVal);
        case vtText:
            return _textVal;
        case vtBlob:
            // 对外用文本展示时, blob 返回十六进制可读形式
            return std::string("<blob:") + std::to_string(_blobVal.size()) + std::string(" bytes>");
        }
        return std::string();
    }

    const std::vector<uint8_t>& asBlob() const {
        return _blobVal;
    }

private:
    Type _type;
    int64_t _intVal;
    double _realVal;
    std::string _textVal;
    std::vector<uint8_t> _blobVal;
};

}  // namespace SQLite
}  // namespace UEAdminAPI
