#include "pfs.grpc.pb.h"
#include "pfs/util.hpp"
#include <grpcpp/grpcpp.h>
#include <filesystem>
#include <iostream>
#include <thread>

namespace pv = pfs::v1;
class StorageService final : public pv::Storage::Service {
 public:
  explicit StorageService(std::filesystem::path root) : root_(std::move(root)) { std::filesystem::create_directories(root_); }
  grpc::Status PutStripe(grpc::ServerContext*, const pv::PutStripeRequest* r, pv::PutStripeResponse* out) override {
    try { pfs::atomic_write(path(*r), r->data()); out->set_size(r->data().size()); out->set_checksum(pfs::checksum(r->data())); return grpc::Status::OK; }
    catch (const std::exception& e) { return {grpc::StatusCode::INTERNAL, e.what()}; }
  }
  grpc::Status GetStripe(grpc::ServerContext*, const pv::GetStripeRequest* r, pv::GetStripeResponse* out) override {
    try { auto data = pfs::read_all(path(*r)); out->set_data(data); out->set_checksum(pfs::checksum(data)); return grpc::Status::OK; }
    catch (const std::exception& e) { return {grpc::StatusCode::NOT_FOUND, e.what()}; }
  }
  grpc::Status DeleteStripe(grpc::ServerContext*, const pv::DeleteStripeRequest* r, pv::Empty*) override {
    std::error_code ec; std::filesystem::remove(path(*r), ec); return grpc::Status::OK;
  }
  grpc::Status Health(grpc::ServerContext*, const pv::Empty*, pv::HealthResponse* out) override { out->set_serving(true); out->set_component("storage"); return grpc::Status::OK; }
 private:
  template<class R> std::filesystem::path path(const R& r) const { return root_ / pfs::safe_name(r.file()) / std::to_string(r.version()) / (std::to_string(r.index()) + ".stripe"); }
  std::filesystem::path root_;
};

int main(int argc, char** argv) {
  std::string listen = argc > 1 ? argv[1] : "0.0.0.0:50052";
  std::string root = argc > 2 ? argv[2] : "./data";
  std::string coordinator = argc > 3 ? argv[3] : "";
  std::string advertise = argc > 4 ? argv[4] : listen;
  std::string id = argc > 5 ? argv[5] : pfs::safe_name(advertise);
  StorageService svc(root); grpc::ServerBuilder b; b.AddListeningPort(listen, grpc::InsecureServerCredentials()); b.RegisterService(&svc);
  auto server = b.BuildAndStart(); if (!server) { std::cerr << "failed to listen on " << listen << '\n'; return 1; }
  if (!coordinator.empty()) {
    auto stub = pv::Coordinator::NewStub(grpc::CreateChannel(coordinator, grpc::InsecureChannelCredentials()));
    pv::RegisterNodeRequest req; req.mutable_node()->set_id(id); req.mutable_node()->set_address(advertise); pv::RegisterNodeResponse res;
    for (int i=0;i<100;++i) { grpc::ClientContext c; auto deadline=std::chrono::system_clock::now()+std::chrono::seconds(1); c.set_deadline(deadline); if (stub->RegisterNode(&c,req,&res).ok()) break; std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
  }
  std::cout << "storage " << id << " listening on " << listen << '\n'; server->Wait();
}
