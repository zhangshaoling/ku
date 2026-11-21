#include "dao/module_store.hpp"
#include "dao/selfhost.hpp"

#include <charconv>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace dao::store {
namespace {

void set_error(dao_error* error, dao_status status, const char* message) {
    if (error == nullptr) return;
    *error = {};
    error->code = status;
    if (message != nullptr) {
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

uint64_t hash_name(std::string_view name) {
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t fingerprint(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}

std::string hex_name(std::string_view name) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash_name(name);
    return out.str();
}

bool valid_name(std::string_view name) {
    dao_value value{};
    return !name.empty() && name.size() <= 1024 &&
           dao_value_make_string_view(
               {reinterpret_cast<const uint8_t*>(name.data()), name.size()}, &value) == DAO_OK;
}

std::string version_text(SemanticVersion version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

bool valid_version(std::string_view text) {
    for (int part = 0; part < 3; ++part) {
        const size_t end = text.find('.');
        const auto field = text.substr(0, end);
        uint32_t value = 0;
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
        if (field.empty() || parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size())
            return false;
        if (part == 2) return end == std::string_view::npos;
        if (end == std::string_view::npos) return false;
        text.remove_prefix(end + 1);
    }
    return false;
}

std::string fingerprint_text(std::string_view value) {
    std::ostringstream text;
    text << std::hex << hash_name(value);
    return text.str();
}

// The lock file stays in place. The OS releases the byte lock on process exit.
class WriterLock {
  public:
    explicit WriterLock(const std::filesystem::path& root) {
        const auto path = root / ".write.lock";
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open module store lock");
        OVERLAPPED overlap{};
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, 1, 0, &overlap)) {
            const DWORD code = GetLastError();
            CloseHandle(handle_);
            throw std::runtime_error(code == ERROR_LOCK_VIOLATION ? "module store is busy"
                                                                  : "cannot lock module store");
        }
#else
        descriptor_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (descriptor_ < 0) throw std::runtime_error("cannot open module store lock");
        if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            const int code = errno;
            close(descriptor_);
            throw std::runtime_error(code == EWOULDBLOCK ? "module store is busy"
                                                        : "cannot lock module store");
        }
#endif
    }

    ~WriterLock() {
#ifdef _WIN32
        OVERLAPPED overlap{};
        UnlockFileEx(handle_, 0, 1, 0, &overlap);
        CloseHandle(handle_);
#else
        flock(descriptor_, LOCK_UN);
        close(descriptor_);
#endif
    }

    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;

  private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

std::filesystem::path manifest_path(const std::filesystem::path& root, std::string_view name,
                                     SemanticVersion version) {
    auto path = root / hex_name(name);
    std::ostringstream filename;
    filename << "v" << version.major << "." << version.minor << "." << version.patch << ".json";
    return path / filename.str();
}

std::filesystem::path module_path(const std::filesystem::path& root, std::string_view name,
                                  SemanticVersion version) {
    std::ostringstream filename;
    filename << "v" << version.major << "." << version.minor << "." << version.patch << ".dao";
    return root / hex_name(name) / filename.str();
}

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>* bytes) {
    if (!std::filesystem::is_regular_file(path)) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) return false;
    input.seekg(0, std::ios::beg);
    bytes->resize(static_cast<size_t>(size));
    return bytes->empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(bytes->data()), size));
}

bool read_text(const std::filesystem::path& path, std::string* text) {
    std::vector<uint8_t> bytes;
    if (!read_file(path, &bytes)) return false;
    text->assign(bytes.begin(), bytes.end());
    return true;
}

bool identity_matches(const dao_module* module, std::string_view name, SemanticVersion version) {
    dao_module_identity identity{};
    identity.struct_size = sizeof(identity);
    if (dao_module_get_identity(module, &identity) != DAO_OK) return false;
    return std::string_view(reinterpret_cast<const char*>(identity.name.data), identity.name.size) == name &&
           identity.version_major == version.major && identity.version_minor == version.minor &&
           identity.version_patch == version.patch;
}

dao_status validate(const std::vector<uint8_t>& bytes, std::string_view name,
                    SemanticVersion version, dao_error* error) {
    if (bytes.empty()) {
        set_error(error, DAO_BAD_MODULE, "stored module is empty");
        return DAO_BAD_MODULE;
    }
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == nullptr) {
        set_error(error, DAO_OUT_OF_MEMORY, "cannot create validation VM");
        return DAO_OUT_OF_MEMORY;
    }
    dao_module* module = nullptr;
    dao_status status = dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, error);
    if (status == DAO_OK && !identity_matches(module, name, version)) {
        set_error(error, DAO_MODULE_CONFLICT, "stored module identity does not match its key");
        status = DAO_MODULE_CONFLICT;
    }
    if (module != nullptr) dao_module_release(module);
    dao_vm_destroy(vm);
    return status;
}

// Schema 1 is a flat object of known fields, with one numeric schema version.
struct Manifest {
    std::string identity;
    std::string version;
    std::string fingerprint;
    std::string source_fingerprint;
    std::string parent_identity;
    std::string parent_version;
};

struct JsonReader {
    std::string_view text;

    bool take(char expected) {
        const auto first = text.find_first_not_of(" \t\r\n");
        text.remove_prefix(first == std::string_view::npos ? text.size() : first);
        if (text.empty() || text.front() != expected) return false;
        text.remove_prefix(1);
        return true;
    }

    bool hex4(uint32_t* value) {
        if (text.size() < 4) return false;
        const auto parsed = std::from_chars(text.data(), text.data() + 4, *value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 4) return false;
        text.remove_prefix(4);
        return true;
    }

    bool string(std::string* result) {
        if (!take('"')) return false;
        result->clear();
        while (!text.empty()) {
            unsigned char byte = static_cast<unsigned char>(text.front());
            text.remove_prefix(1);
            if (byte == '"') return true;
            if (byte < 0x20) return false;
            if (byte != '\\') { result->push_back(static_cast<char>(byte)); continue; }
            if (text.empty()) return false;
            const char escaped = text.front();
            text.remove_prefix(1);
            switch (escaped) {
            case '"': case '\\': case '/': result->push_back(escaped); break;
            case 'b': result->push_back('\b'); break;
            case 'f': result->push_back('\f'); break;
            case 'n': result->push_back('\n'); break;
            case 'r': result->push_back('\r'); break;
            case 't': result->push_back('\t'); break;
            case 'u': {
                uint32_t code = 0;
                if (!hex4(&code)) return false;
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (!text.starts_with("\\u")) return false;
                    text.remove_prefix(2);
                    uint32_t low = 0;
                    if (!hex4(&low) || low < 0xdc00 || low > 0xdfff) return false;
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                if (code < 0x80) result->push_back(static_cast<char>(code));
                else if (code < 0x800) {
                    result->push_back(static_cast<char>(0xc0 | (code >> 6)));
                    result->push_back(static_cast<char>(0x80 | (code & 0x3f)));
                } else if (code < 0x10000) {
                    result->push_back(static_cast<char>(0xe0 | (code >> 12)));
                    result->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                    result->push_back(static_cast<char>(0x80 | (code & 0x3f)));
                } else {
                    result->push_back(static_cast<char>(0xf0 | (code >> 18)));
                    result->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
                    result->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                    result->push_back(static_cast<char>(0x80 | (code & 0x3f)));
                }
                break;
            }
            default: return false;
            }
        }
        return false;
    }
};

std::string json_string(std::string_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
        if (byte == '"' || byte == '\\') result.push_back('\\');
        if (byte < 0x20) {
            result += "\\u00";
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0xf]);
        } else result.push_back(static_cast<char>(byte));
    }
    result.push_back('"');
    return result;
}

bool read_manifest(const std::filesystem::path& path, std::string_view name,
                   SemanticVersion version, uint64_t expected_fingerprint, Manifest* manifest) {
    std::string text;
    if (!read_text(path, &text)) return false;
    JsonReader reader{text};
    if (!reader.take('{')) return false;
    std::map<std::string, std::string> fields;
    do {
        std::string key, value;
        if (!reader.string(&key) || !reader.take(':')) return false;
        if (key == "schema_version") {
            if (!reader.take('1')) return false;
            value = "1";
        } else {
            if (key != "identity" && key != "version" && key != "fingerprint" &&
                key != "source_fingerprint" && key != "parent_identity" && key != "parent_version")
                return false;
            if (!reader.string(&value)) return false;
        }
        const auto [existing, inserted] = fields.emplace(key, value);
        if (!inserted) {
            // Early schema-1 writers appended identical parent fields on retry.
            if ((key != "parent_identity" && key != "parent_version") || existing->second != value)
                return false;
        }
        if (reader.take('}')) break;
        if (!reader.take(',')) return false;
    } while (true);
    if (reader.text.find_first_not_of(" \t\r\n") != std::string_view::npos ||
        !fields.contains("schema_version") || fields["identity"] != name ||
        fields["version"] != version_text(version)) return false;
    uint64_t checksum = 0;
    const auto& digest = fields["fingerprint"];
    const auto parsed = std::from_chars(digest.data(), digest.data() + digest.size(), checksum, 16);
    if (digest.empty() || parsed.ec != std::errc{} || parsed.ptr != digest.data() + digest.size() ||
        checksum != expected_fingerprint) return false;
    const bool has_parent = fields.contains("parent_identity");
    if (has_parent != fields.contains("parent_version") ||
        (has_parent && (!valid_name(fields["parent_identity"]) || !valid_version(fields["parent_version"]))))
        return false;
    if (fields.contains("source_fingerprint")) {
        const auto& source_digest = fields["source_fingerprint"];
        const auto source_parsed = std::from_chars(
            source_digest.data(), source_digest.data() + source_digest.size(), checksum, 16);
        if (source_digest.empty() || source_parsed.ec != std::errc{} ||
            source_parsed.ptr != source_digest.data() + source_digest.size()) return false;
        std::ostringstream canonical;
        canonical << std::hex << checksum;
        fields["source_fingerprint"] = canonical.str();
    }
    *manifest = {fields["identity"], fields["version"], digest,
                 fields["source_fingerprint"], fields["parent_identity"], fields["parent_version"]};
    return true;
}

std::string encode_manifest(const Manifest& manifest) {
    std::string text = "{\"schema_version\":1,\"identity\":" + json_string(manifest.identity) +
                       ",\"version\":" + json_string(manifest.version) +
                       ",\"fingerprint\":" + json_string(manifest.fingerprint);
    if (!manifest.source_fingerprint.empty())
        text += ",\"source_fingerprint\":" + json_string(manifest.source_fingerprint);
    if (!manifest.parent_identity.empty())
        text += ",\"parent_identity\":" + json_string(manifest.parent_identity) +
                ",\"parent_version\":" + json_string(manifest.parent_version);
    return text + "}";
}

void write_atomic(const std::filesystem::path& path, std::string_view contents) {
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open module store temporary file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) throw std::runtime_error("cannot write module store temporary file");
    std::filesystem::rename(temporary, path);
}

dao_status compile_source(std::string_view compiler_source, std::string_view source,
                          std::string_view identity_name, SemanticVersion version,
                          std::vector<uint8_t>* bytes, dao_error* error) {
    if (!valid_name(identity_name) || compiler_source.empty() || source.empty()) {
        set_error(error, DAO_INVALID_ARGUMENT, "compiler, source, and identity are required");
        return DAO_INVALID_ARGUMENT;
    }
    dao::selfhost::Options options;
    options.has_identity = true;
    options.identity_name = std::string(identity_name);
    options.identity_version = version;
    if (!dao::selfhost::compile(compiler_source, source, bytes, error, std::move(options)))
        return error == nullptr ? DAO_RUNTIME_ERROR : error->code;
    return DAO_OK;
}

} // namespace

ModuleStore::ModuleStore(std::filesystem::path root) : root_(std::move(root)) {}

dao_status ModuleStore::save(const std::vector<uint8_t>& bytes, std::string_view source,
                             dao_error* error) const {
    return save_record(bytes, source, {}, {}, error);
}

dao_status ModuleStore::save_record(const std::vector<uint8_t>& bytes, std::string_view source,
                                    std::string_view parent_identity, SemanticVersion parent_version,
                                    dao_error* error) const {
    if (bytes.empty()) {
        set_error(error, DAO_INVALID_ARGUMENT, "module bytes are empty");
        return DAO_INVALID_ARGUMENT;
    }
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == nullptr) { set_error(error, DAO_OUT_OF_MEMORY, "cannot create module store VM"); return DAO_OUT_OF_MEMORY; }
    dao_module* module = nullptr;
    dao_status status = dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, error);
    if (status != DAO_OK) { dao_vm_destroy(vm); return status; }
    dao_module_identity identity{}; identity.struct_size = sizeof(identity);
    status = dao_module_get_identity(module, &identity);
    if (status != DAO_OK) {
        dao_module_release(module);
        dao_vm_destroy(vm);
        set_error(error, status, "stored module requires an identity");
        return status;
    }
    const std::string name(reinterpret_cast<const char*>(identity.name.data), identity.name.size);
    const SemanticVersion version{identity.version_major, identity.version_minor, identity.version_patch};
    dao_module_release(module); dao_vm_destroy(vm);
    const auto final_path = module_path(root_, name, version);
    try {
        std::filesystem::create_directories(root_);
        WriterLock lock(root_);
        std::filesystem::create_directories(final_path.parent_path());
        const uint64_t fingerprint_value = fingerprint(bytes);
        const auto manifest = manifest_path(root_, name, version);
        auto source_path = final_path;
        source_path.replace_extension(".ku");
        Manifest metadata{name, version_text(version),
                          fingerprint_text({reinterpret_cast<const char*>(bytes.data()), bytes.size()}),
                          {}, std::string(parent_identity),
                          parent_identity.empty() ? std::string{} : version_text(parent_version)};
        if (!parent_identity.empty()) {
            if (name == parent_identity && metadata.version == metadata.parent_version) {
                set_error(error, DAO_MODULE_CYCLE, "module cannot be its own parent");
                return DAO_MODULE_CYCLE;
            }
            std::vector<uint8_t> parent_bytes;
            const dao_status parent_status = load(parent_identity, parent_version, &parent_bytes, error);
            if (parent_status != DAO_OK) return parent_status;
        }

        std::vector<uint8_t> existing;
        const bool module_exists = std::filesystem::exists(final_path);
        if (module_exists) {
            if (!read_file(final_path, &existing))
                throw std::runtime_error("cannot read stored module");
            if (existing != bytes) {
                set_error(error, DAO_MODULE_CONFLICT, "module identity/version already exists with different bytes");
                return DAO_MODULE_CONFLICT;
            }
            if (std::filesystem::exists(manifest)) {
                Manifest committed;
                if (!read_manifest(manifest, name, version, fingerprint_value, &committed)) {
                    set_error(error, DAO_BAD_MODULE, "invalid module manifest");
                    return DAO_BAD_MODULE;
                }
                if (!parent_identity.empty() &&
                    (committed.parent_identity != parent_identity ||
                     committed.parent_version != metadata.parent_version)) {
                    set_error(error, DAO_MODULE_CONFLICT, "module parent provenance conflicts with stored record");
                    return DAO_MODULE_CONFLICT;
                }
                metadata = std::move(committed);
            }
        }
        const bool committed = std::filesystem::exists(manifest);
        if (committed && !module_exists) {
            set_error(error, DAO_BAD_MODULE, "module manifest exists without its module");
            return DAO_BAD_MODULE;
        }

        std::string stored_source;
        const bool source_exists = std::filesystem::exists(source_path);
        if (source_exists && !read_text(source_path, &stored_source))
            throw std::runtime_error("cannot read stored source provenance");
        if (!metadata.source_fingerprint.empty() && source_exists &&
            fingerprint_text(stored_source) != metadata.source_fingerprint) {
            set_error(error, DAO_BAD_MODULE, "stored source fingerprint mismatch");
            return DAO_BAD_MODULE;
        }
        if (!source.empty() &&
            ((source_exists && stored_source != source) ||
             (!metadata.source_fingerprint.empty() && fingerprint_text(source) != metadata.source_fingerprint))) {
            set_error(error, DAO_MODULE_CONFLICT, "module source provenance conflicts with stored record");
            return DAO_MODULE_CONFLICT;
        }
        if (committed && !metadata.source_fingerprint.empty() && !source_exists && source.empty()) {
            set_error(error, DAO_BAD_MODULE, "stored source provenance is missing");
            return DAO_BAD_MODULE;
        }
        if (!module_exists)
            write_atomic(final_path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        if (!source.empty() && !source_exists) write_atomic(source_path, source);
        if (committed) return DAO_OK;

        if (!source.empty() || source_exists)
            metadata.source_fingerprint = fingerprint_text(source.empty() ? stored_source : source);
        // Publishing the complete manifest is the final commit step.
        write_atomic(manifest, encode_manifest(metadata));
        return DAO_OK;
    } catch (const std::filesystem::filesystem_error&) {
        set_error(error, DAO_RUNTIME_ERROR, "module store filesystem operation failed"); return DAO_RUNTIME_ERROR;
    } catch (const std::exception& exception) {
        set_error(error, DAO_RUNTIME_ERROR, exception.what()); return DAO_RUNTIME_ERROR;
    } catch (...) {
        set_error(error, DAO_RUNTIME_ERROR, "module store write failed"); return DAO_RUNTIME_ERROR;
    }
}

dao_status ModuleStore::save_derived_source(std::string_view compiler_source, std::string_view source,
                                           std::string_view identity_name, SemanticVersion version,
                                           std::string_view parent_identity, SemanticVersion parent_version,
                                           dao_error* error) const {
    if (!valid_name(parent_identity)) {
        set_error(error, DAO_INVALID_ARGUMENT, "parent identity is invalid");
        return DAO_INVALID_ARGUMENT;
    }
    std::vector<uint8_t> bytes;
    const dao_status status = compile_source(compiler_source, source, identity_name, version, &bytes, error);
    if (status != DAO_OK) return status;
    return save_record(bytes, source, parent_identity, parent_version, error);
}

dao_status ModuleStore::save_source(std::string_view compiler_source, std::string_view source,
                                   std::string_view identity_name, SemanticVersion version,
                                   dao_error* error) const {
    std::vector<uint8_t> bytes;
    const dao_status status = compile_source(compiler_source, source, identity_name, version, &bytes, error);
    if (status != DAO_OK) return status;
    return save(bytes, source, error);
}

dao_status ModuleStore::verify(std::string_view identity_name, SemanticVersion version,
                               dao_error* error) const {
    std::vector<uint8_t> bytes;
    return load(identity_name, version, &bytes, error);
}

dao_status ModuleStore::load(std::string_view identity_name, SemanticVersion version,
                             std::vector<uint8_t>* bytes, dao_error* error) const {
    if (bytes != nullptr) bytes->clear();
    if (bytes == nullptr || !valid_name(identity_name)) { set_error(error, DAO_INVALID_ARGUMENT, "invalid module identity"); return DAO_INVALID_ARGUMENT; }
    try {
        std::vector<uint8_t> loaded;
        const auto path = module_path(root_, identity_name, version);
        if (!read_file(path, &loaded)) {
            const dao_status status = std::filesystem::exists(path) ? DAO_RUNTIME_ERROR : DAO_IMPORT_NOT_FOUND;
            set_error(error, status, "module cannot be read or is not stored");
            return status;
        }
        const dao_status status = validate(loaded, identity_name, version, error);
        if (status != DAO_OK) return status;
        Manifest metadata;
        if (!read_manifest(manifest_path(root_, identity_name, version), identity_name, version,
                           fingerprint(loaded), &metadata)) {
            set_error(error, DAO_BAD_MODULE, "invalid module manifest");
            return DAO_BAD_MODULE;
        }
        if (!metadata.source_fingerprint.empty()) {
            auto source_path = path;
            source_path.replace_extension(".ku");
            std::string source;
            if (!read_text(source_path, &source) || fingerprint_text(source) != metadata.source_fingerprint) {
                set_error(error, DAO_BAD_MODULE, "stored source provenance is missing or corrupt");
                return DAO_BAD_MODULE;
            }
        }
        *bytes = std::move(loaded);
        return DAO_OK;
    } catch (const std::exception& exception) {
        set_error(error, DAO_RUNTIME_ERROR, exception.what());
        return DAO_RUNTIME_ERROR;
    }
}

} // namespace dao::store
