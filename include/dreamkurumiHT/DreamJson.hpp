#pragma once

// ============================================================================
// DreamJson.hpp - High-Performance C++20 JSON Engine powered by yyjson
//
// Part of the dreamkurumiHT Framework
// Blazing fast JSON parsing & serialization at gigabytes per second!
// Perfect for reflection data, mod manifests, save games, configs, & network RPC.
// ============================================================================

#include "yyjson/yyjson.h"
#include "DreamLog.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>

namespace dreamkurumi {

class DreamJsonValue;

class DreamJsonValue {
private:
    yyjson_val* m_val = nullptr;

public:
    DreamJsonValue() : m_val(nullptr) {}
    DreamJsonValue(yyjson_val* val) : m_val(val) {}

    bool is_valid() const { return m_val != nullptr; }
    bool is_null() const { return m_val && yyjson_is_null(m_val); }
    bool is_bool() const { return m_val && yyjson_is_bool(m_val); }
    bool is_true() const { return m_val && yyjson_is_true(m_val); }
    bool is_int() const { return m_val && yyjson_is_int(m_val); }
    bool is_uint() const { return m_val && yyjson_is_uint(m_val); }
    bool is_real() const { return m_val && yyjson_is_real(m_val); }
    bool is_str() const { return m_val && yyjson_is_str(m_val); }
    bool is_arr() const { return m_val && yyjson_is_arr(m_val); }
    bool is_obj() const { return m_val && yyjson_is_obj(m_val); }

    bool as_bool(bool def = false) const {
        if (!m_val || !yyjson_is_bool(m_val)) return def;
        return yyjson_get_bool(m_val);
    }

    int64_t as_int(int64_t def = 0) const {
        if (!m_val) return def;
        if (yyjson_is_int(m_val)) return yyjson_get_sint(m_val);
        if (yyjson_is_uint(m_val)) return static_cast<int64_t>(yyjson_get_uint(m_val));
        if (yyjson_is_real(m_val)) return static_cast<int64_t>(yyjson_get_real(m_val));
        return def;
    }

    uint64_t as_uint(uint64_t def = 0) const {
        if (!m_val) return def;
        if (yyjson_is_uint(m_val)) return yyjson_get_uint(m_val);
        if (yyjson_is_int(m_val)) return static_cast<uint64_t>(yyjson_get_sint(m_val));
        return def;
    }

    double as_double(double def = 0.0) const {
        if (!m_val) return def;
        if (yyjson_is_real(m_val)) return yyjson_get_real(m_val);
        if (yyjson_is_int(m_val)) return static_cast<double>(yyjson_get_sint(m_val));
        if (yyjson_is_uint(m_val)) return static_cast<double>(yyjson_get_uint(m_val));
        return def;
    }

    float as_float(float def = 0.0f) const {
        return static_cast<float>(as_double(def));
    }

    std::string as_string(const std::string& def = "") const {
        if (!m_val || !yyjson_is_str(m_val)) return def;
        const char* s = yyjson_get_str(m_val);
        return s ? std::string(s) : def;
    }

    const char* as_c_str(const char* def = "") const {
        if (!m_val || !yyjson_is_str(m_val)) return def;
        const char* s = yyjson_get_str(m_val);
        return s ? s : def;
    }

    size_t size() const {
        if (!m_val) return 0;
        if (yyjson_is_arr(m_val)) return yyjson_arr_size(m_val);
        if (yyjson_is_obj(m_val)) return yyjson_obj_size(m_val);
        return 0;
    }

    bool has(const char* key) const {
        if (!m_val || !yyjson_is_obj(m_val) || !key) return false;
        return yyjson_obj_get(m_val, key) != nullptr;
    }

    DreamJsonValue operator[](const char* key) const {
        if (!m_val || !yyjson_is_obj(m_val) || !key) return DreamJsonValue(nullptr);
        return DreamJsonValue(yyjson_obj_get(m_val, key));
    }

    DreamJsonValue operator[](const std::string& key) const {
        return operator[](key.c_str());
    }

    DreamJsonValue operator[](size_t index) const {
        if (!m_val || !yyjson_is_arr(m_val)) return DreamJsonValue(nullptr);
        return DreamJsonValue(yyjson_arr_get(m_val, index));
    }

    DreamJsonValue operator[](int index) const {
        if (index < 0) return DreamJsonValue(nullptr);
        return operator[](static_cast<size_t>(index));
    }

    yyjson_val* raw() const { return m_val; }
};

class DreamJsonDocument {
private:
    yyjson_doc* m_doc = nullptr;

public:
    DreamJsonDocument() : m_doc(nullptr) {}
    explicit DreamJsonDocument(yyjson_doc* doc) : m_doc(doc) {}

    ~DreamJsonDocument() {
        if (m_doc) {
            yyjson_doc_free(m_doc);
            m_doc = nullptr;
        }
    }

    // Move semantics
    DreamJsonDocument(DreamJsonDocument&& other) noexcept : m_doc(other.m_doc) {
        other.m_doc = nullptr;
    }

    DreamJsonDocument& operator=(DreamJsonDocument&& other) noexcept {
        if (this != &other) {
            if (m_doc) yyjson_doc_free(m_doc);
            m_doc = other.m_doc;
            other.m_doc = nullptr;
        }
        return *this;
    }

    // Disallow copying
    DreamJsonDocument(const DreamJsonDocument&) = delete;
    DreamJsonDocument& operator=(const DreamJsonDocument&) = delete;

    bool is_valid() const { return m_doc != nullptr; }

    DreamJsonValue root() const {
        if (!m_doc) return DreamJsonValue(nullptr);
        return DreamJsonValue(yyjson_doc_get_root(m_doc));
    }

    DreamJsonValue operator[](const char* key) const {
        return root()[key];
    }

    DreamJsonValue operator[](size_t index) const {
        return root()[index];
    }

    yyjson_doc* raw() const { return m_doc; }
};

class DreamJsonMutDocument {
private:
    yyjson_mut_doc* m_mutDoc = nullptr;
    yyjson_mut_val* m_root = nullptr;

public:
    DreamJsonMutDocument() {
        m_mutDoc = yyjson_mut_doc_new(nullptr);
        m_root = yyjson_mut_obj(m_mutDoc);
        yyjson_mut_doc_set_root(m_mutDoc, m_root);
    }

    explicit DreamJsonMutDocument(bool asArray) {
        m_mutDoc = yyjson_mut_doc_new(nullptr);
        m_root = asArray ? yyjson_mut_arr(m_mutDoc) : yyjson_mut_obj(m_mutDoc);
        yyjson_mut_doc_set_root(m_mutDoc, m_root);
    }

    ~DreamJsonMutDocument() {
        if (m_mutDoc) {
            yyjson_mut_doc_free(m_mutDoc);
            m_mutDoc = nullptr;
        }
    }

    DreamJsonMutDocument(DreamJsonMutDocument&& other) noexcept 
        : m_mutDoc(other.m_mutDoc), m_root(other.m_root) {
        other.m_mutDoc = nullptr;
        other.m_root = nullptr;
    }

    DreamJsonMutDocument& operator=(DreamJsonMutDocument&& other) noexcept {
        if (this != &other) {
            if (m_mutDoc) yyjson_mut_doc_free(m_mutDoc);
            m_mutDoc = other.m_mutDoc;
            m_root = other.m_root;
            other.m_mutDoc = nullptr;
            other.m_root = nullptr;
        }
        return *this;
    }

    DreamJsonMutDocument(const DreamJsonMutDocument&) = delete;
    DreamJsonMutDocument& operator=(const DreamJsonMutDocument&) = delete;

    bool is_valid() const { return m_mutDoc != nullptr; }

    void add(const char* key, const std::string& val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_str(m_mutDoc, m_root, key, val.c_str());
    }

    void add(const char* key, const char* val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_str(m_mutDoc, m_root, key, val ? val : "");
    }

    void add(const char* key, int64_t val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_sint(m_mutDoc, m_root, key, val);
    }

    void add(const char* key, uint64_t val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_uint(m_mutDoc, m_root, key, val);
    }

    void add(const char* key, double val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_real(m_mutDoc, m_root, key, val);
    }

    void add(const char* key, bool val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_bool(m_mutDoc, m_root, key, val);
    }

    void add_null(const char* key) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_obj_add_null(m_mutDoc, m_root, key);
    }

    // Array operations
    void push_back(const std::string& val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_arr_add_str(m_mutDoc, m_root, val.c_str());
    }

    void push_back(int64_t val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_arr_add_sint(m_mutDoc, m_root, val);
    }

    void push_back(double val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_arr_add_real(m_mutDoc, m_root, val);
    }

    void push_back(bool val) {
        if (!m_mutDoc || !m_root) return;
        yyjson_mut_arr_add_bool(m_mutDoc, m_root, val);
    }

    std::string stringify(bool pretty = false) const {
        if (!m_mutDoc) return "{}";
        yyjson_write_flag flg = pretty ? YYJSON_WRITE_PRETTY : 0;
        size_t len = 0;
        char* json = yyjson_mut_write(m_mutDoc, flg, &len);
        if (!json) return "{}";
        std::string res(json, len);
        free(json);
        return res;
    }

    yyjson_mut_doc* raw_doc() const { return m_mutDoc; }
    yyjson_mut_val* raw_root() const { return m_root; }
};

class DreamJson {
public:
    static DreamJsonDocument Parse(std::string_view jsonStr) {
        if (jsonStr.empty()) return DreamJsonDocument(nullptr);
        yyjson_read_flag flg = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS;
        yyjson_doc* doc = yyjson_read(jsonStr.data(), jsonStr.size(), flg);
        return DreamJsonDocument(doc);
    }

    static DreamJsonDocument ParseFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return DreamJsonDocument(nullptr);
        }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return Parse(content);
    }

    static std::string Stringify(const DreamJsonDocument& doc, bool pretty = false) {
        if (!doc.is_valid()) return "{}";
        yyjson_write_flag flg = pretty ? YYJSON_WRITE_PRETTY : 0;
        size_t len = 0;
        char* json = yyjson_write(doc.raw(), flg, &len);
        if (!json) return "{}";
        std::string res(json, len);
        free(json);
        return res;
    }

    static bool SaveToFile(const std::string& filepath, const DreamJsonMutDocument& doc, bool pretty = true) {
        std::string content = doc.stringify(pretty);
        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(content.data(), content.size());
        return true;
    }
};

} // namespace dreamkurumi
