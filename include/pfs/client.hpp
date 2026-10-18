#pragma once
#include <grpcpp/grpcpp.h>
#include "pfs.grpc.pb.h"
#include <memory>
#include <string>
#include <vector>

namespace pfs {
class Client {
 public:
  explicit Client(std::string coordinator, std::string id = "");
  void write(const std::string& remote, const std::string& data, uint32_t stripe_size = 1024 * 1024);
  std::string read(const std::string& remote);
  void remove(const std::string& remote);
  std::vector<pfs::v1::FileInfo> list();
 private:
  struct Token { std::string file, value; };
  Token acquire(const std::string&, pfs::v1::TokenMode);
  void release(const Token&) noexcept;
  std::unique_ptr<pfs::v1::Coordinator::Stub> coordinator_;
  std::string id_;
};
}
