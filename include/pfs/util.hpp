#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace pfs {
inline uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}
inline std::string uuid() {
  thread_local std::mt19937_64 rng(std::random_device{}());
  std::ostringstream out;
  out << std::hex << now_ms() << '-' << rng() << '-' << rng();
  return out.str();
}
inline uint64_t fnv1a(const std::string& data) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : data) { h ^= c; h *= 1099511628211ULL; }
  return h;
}
inline std::string checksum(const std::string& data) {
  std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << fnv1a(data); return out.str();
}
inline std::string safe_name(const std::string& s) {
  std::ostringstream out; out << std::hex << fnv1a(s); return out.str();
}
inline void atomic_write(const std::filesystem::path& path, const std::string& data) {
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
  auto tmp = path; tmp += ".tmp." + uuid();
  { std::ofstream f(tmp, std::ios::binary); if (!f) throw std::runtime_error("cannot open " + tmp.string()); f.write(data.data(), static_cast<std::streamsize>(data.size())); if (!f) throw std::runtime_error("write failed"); }
  std::filesystem::rename(tmp, path);
}
inline std::string read_all(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary); if (!f) throw std::runtime_error("cannot open " + path.string());
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
}
