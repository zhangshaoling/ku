#include "dao/module_store.hpp"
#include "dao/selfhost.hpp"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

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

bool valid_name(std::string_view name) { return !name.empty() && name.size() <= 1024; }

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

bool read_text(const std::filesystem::path& path, std::string* text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *text = buffer.str();
    return true;
}

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>* bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) return false;
    input.seekg(0, std::ios::beg);
    bytes->resize(static_cast<size_t>(size));
    return bytes->empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(bytes->data()), size));
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

bool manifest_matches(const std::filesystem::path& path, std::string_view name,
                      SemanticVersion version, uint64_t expected_fingerprint) {
    std::string text;
    if (!read_text(path, &text)) return false;
    std::ostringstream version_text;
    version_text << version.major << "." << version.minor << "." << version.patch;
    std::ostringstream fingerprint_text;
    fingerprint_text << std::hex << expected_fingerprint;
    return text.find("\"schema_version\":1") != std::string::npos &&
           text.find("\"identity\":\"" + std::string(name) + "\"") != std::string::npos &&
           text.find("\"version\":\"" + version_text.str() + "\"") != std::string::npos &&
           text.find("\"fingerprint\":\"" + fingerprint_text.str() + "\"") != std::string::npos;
}

} // namespace

ModuleStore::ModuleStore(std::filesystem::path root) : root_(std::move(root)) {}

dao_status ModuleStore::save(const std::vector<uint8_t>& bytes, std::string_view source,
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
    if (status != DAO_OK) { dao_module_release(module); dao_vm_destroy(vm); return status; }
    const std::string name(reinterpret_cast<const char*>(identity.name.data), identity.name.size);
    const SemanticVersion version{identity.version_major, identity.version_minor, identity.version_patch};
    dao_module_release(module); dao_vm_destroy(vm);
    const auto final_path = module_path(root_, name, version);
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::filesystem::create_directories(final_path.parent_path());
        const uint64_t fingerprint_value = fingerprint(bytes);
        const auto manifest = manifest_path(root_, name, version);
        std::vector<uint8_t> existing;
        if (read_file(final_path, &existing)) {
            if (existing != bytes) {
                set_error(error, DAO_MODULE_CONFLICT, "module identity/version already exists with different bytes");
                return DAO_MODULE_CONFLICT;
            }
            if (std::filesystem::exists(manifest)) {
                if (manifest_matches(manifest, name, version, fingerprint_value)) return DAO_OK;
                set_error(error, DAO_BAD_MODULE, "module manifest fingerprint or identity mismatch");
                return DAO_BAD_MODULE;
            }
            // The module was published before an interrupted manifest write; finish it.
        } else {
            const auto temporary = final_path.string() + ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot open module temporary");
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output) throw std::runtime_error("cannot write module temporary");
            std::filesystem::rename(temporary, final_path);
        }
        std::ofstream manifest_file(manifest.string() + ".tmp", std::ios::binary | std::ios::trunc);
        if (!manifest_file) throw std::runtime_error("cannot open module manifest temporary");
        manifest_file << "{\"schema_version\":1,\"identity\":\"" << name
                      << "\",\"version\":\"" << version.major << "." << version.minor
                      << "." << version.patch << "\",\"fingerprint\":\"" << std::hex
                      << fingerprint_value << "\"}";
        manifest_file.close();
        if (!manifest_file) throw std::runtime_error("cannot write module manifest temporary");
        std::filesystem::rename(manifest.string() + ".tmp", manifest);
        if (!source.empty()) { auto source_path = final_path; source_path.replace_extension(".ku"); std::ofstream source_file(source_path, std::ios::binary | std::ios::trunc); source_file.write(source.data(), static_cast<std::streamsize>(source.size())); }
        return DAO_OK;
    } catch (const std::filesystem::filesystem_error&) {
        set_error(error, DAO_RUNTIME_ERROR, "module store filesystem operation failed"); return DAO_RUNTIME_ERROR;
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
    const dao_status status = save_source(compiler_source, source, identity_name, version, error);
    if (status != DAO_OK) return status;
    const auto manifest = manifest_path(root_, identity_name, version);
    std::string text;
    if (!read_text(manifest, &text)) {
        set_error(error, DAO_RUNTIME_ERROR, "module manifest cannot be read");
        return DAO_RUNTIME_ERROR;
    }
    const auto marker = std::string("}");
    if (text.empty() || text.back() != '}') {
        set_error(error, DAO_BAD_MODULE, "module manifest is malformed");
        return DAO_BAD_MODULE;
    }
    text.pop_back();
    std::ostringstream parent;
    parent << ",\"parent_identity\":\"" << parent_identity << "\",\"parent_version\":\""
           << parent_version.major << "." << parent_version.minor << "." << parent_version.patch << "\"}";
    text += parent.str();
    std::ofstream output(manifest.string() + ".tmp", std::ios::binary | std::ios::trunc);
    if (!output) { set_error(error, DAO_RUNTIME_ERROR, "module manifest cannot be updated"); return DAO_RUNTIME_ERROR; }
    output << text;
    output.close();
    std::filesystem::rename(manifest.string() + ".tmp", manifest);
    return DAO_OK;
}

dao_status ModuleStore::save_source(std::string_view compiler_source, std::string_view source,
                                   std::string_view identity_name, SemanticVersion version,
                                   dao_error* error) const {
    if (!valid_name(identity_name) || compiler_source.empty() || source.empty()) {
        set_error(error, DAO_INVALID_ARGUMENT, "compiler, source, and identity are required");
        return DAO_INVALID_ARGUMENT;
    }
    std::vector<uint8_t> bytes;
    dao::selfhost::Options options;
    options.has_identity = true;
    options.identity_name = std::string(identity_name);
    options.identity_version = version;
    if (!dao::selfhost::compile(compiler_source, source, &bytes, error, std::move(options)))
        return error == nullptr ? DAO_RUNTIME_ERROR : error->code;
    return save(bytes, source, error);
}

dao_status ModuleStore::verify(std::string_view identity_name, SemanticVersion version,
                               dao_error* error) const {
    std::vector<uint8_t> bytes;
    return load(identity_name, version, &bytes, error);
}

dao_status ModuleStore::load(std::string_view identity_name, SemanticVersion version,
                             std::vector<uint8_t>* bytes, dao_error* error) const {
    if (bytes == nullptr || !valid_name(identity_name)) { set_error(error, DAO_INVALID_ARGUMENT, "invalid module identity"); return DAO_INVALID_ARGUMENT; }
    bytes->clear();
    if (!read_file(module_path(root_, identity_name, version), bytes)) { set_error(error, DAO_IMPORT_NOT_FOUND, "module is not stored"); return DAO_IMPORT_NOT_FOUND; }
    const dao_status status = validate(*bytes, identity_name, version, error);
    if (status != DAO_OK) return status;
    if (!manifest_matches(manifest_path(root_, identity_name, version), identity_name, version,
                          fingerprint(*bytes))) {
        set_error(error, DAO_BAD_MODULE, "module manifest fingerprint or identity mismatch");
        bytes->clear();
        return DAO_BAD_MODULE;
    }
    return DAO_OK;
}

} // namespace dao::store
