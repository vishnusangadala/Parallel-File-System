#include "pfs.grpc.pb.h"
#include "pfs/util.hpp"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace pv = pfs::v1;
class CoordinatorService final : public pv::Coordinator::Service {
  struct Held { std::string value, client; pv::TokenMode mode; uint64_t expiry; };
  struct NodeRec { std::string id, address; };
 public:
  explicit CoordinatorService(std::string state_file) : state_file_(std::move(state_file)) {
    if (state_file_.empty() || !std::filesystem::exists(state_file_)) return;
    pv::MetadataSnapshot snap; auto bytes=pfs::read_all(state_file_); if(!snap.ParseFromString(bytes)) throw std::runtime_error("invalid metadata snapshot");
    for(auto& l:snap.files()){files_[l.name()]=l;versions_[l.name()]=std::max(versions_[l.name()],l.version());}
  }
  grpc::Status RegisterNode(grpc::ServerContext*, const pv::RegisterNodeRequest* r, pv::RegisterNodeResponse* out) override {
    if (r->node().id().empty() || r->node().address().empty()) return {grpc::StatusCode::INVALID_ARGUMENT,"node id/address required"};
    std::lock_guard lk(mu_); nodes_[r->node().id()] = NodeRec{r->node().id(), r->node().address()}; out->set_node_count(static_cast<uint32_t>(nodes_.size())); cv_.notify_all(); return grpc::Status::OK;
  }
  grpc::Status AcquireToken(grpc::ServerContext* ctx, const pv::AcquireTokenRequest* r, pv::AcquireTokenResponse* out) override {
    if (r->file().empty() || r->client_id().empty() || r->mode()==pv::TOKEN_MODE_UNSPECIFIED) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid token request"};
    auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(r->timeout_ms()?r->timeout_ms():30000); std::unique_lock lk(mu_);
    while (true) {
      purge(r->file()); auto& hs=tokens_[r->file()]; bool ok=hs.empty() || (r->mode()==pv::TOKEN_MODE_READ && std::all_of(hs.begin(),hs.end(),[](auto& h){return h.mode==pv::TOKEN_MODE_READ;}));
      if (ok) { Held h{pfs::uuid(),r->client_id(),r->mode(),pfs::now_ms()+(r->lease_ms()?r->lease_ms():60000)}; hs.push_back(h); out->set_token(h.value); out->set_expires_unix_ms(h.expiry); return grpc::Status::OK; }
      if (ctx->IsCancelled()) return {grpc::StatusCode::CANCELLED,"cancelled"};
      if (cv_.wait_until(lk,until)==std::cv_status::timeout) return {grpc::StatusCode::DEADLINE_EXCEEDED,"token wait timed out"};
    }
  }
  grpc::Status ReleaseToken(grpc::ServerContext*, const pv::ReleaseTokenRequest* r, pv::Empty*) override { std::lock_guard lk(mu_); auto& hs=tokens_[r->file()]; std::erase_if(hs,[&](auto& h){return h.value==r->token();}); cv_.notify_all(); return grpc::Status::OK; }
  grpc::Status ReserveWrite(grpc::ServerContext*, const pv::ReserveWriteRequest* r, pv::FileLayout* out) override {
    std::lock_guard lk(mu_); if (!valid(r->name(),r->token(),pv::TOKEN_MODE_WRITE)) return denied(); if(nodes_.empty()) return {grpc::StatusCode::FAILED_PRECONDITION,"no storage nodes"};
    auto stripe=r->stripe_size()?r->stripe_size():1024*1024; out->set_name(r->name()); out->set_size(r->size()); out->set_stripe_size(stripe); out->set_version(++versions_[r->name()]); out->set_write_id(pfs::uuid());
    std::vector<NodeRec> ns; for(auto& [_,n]:nodes_) ns.push_back(n); std::sort(ns.begin(),ns.end(),[](auto&a,auto&b){return a.id<b.id;});
    uint64_t count=(r->size()+stripe-1)/stripe; for(uint64_t i=0;i<count;++i){ auto*s=out->add_stripes(); auto&n=ns[i%ns.size()]; s->set_index(i); s->set_node_id(n.id); s->set_node_address(n.address); s->set_size(std::min<uint64_t>(stripe,r->size()-i*stripe)); }
    pending_[out->write_id()]=*out; return grpc::Status::OK;
  }
  grpc::Status CommitWrite(grpc::ServerContext*, const pv::CommitWriteRequest* r, pv::Empty*) override {
    std::lock_guard lk(mu_); auto& l=r->layout(); if(!valid(l.name(),r->token(),pv::TOKEN_MODE_WRITE)) return denied(); auto it=pending_.find(l.write_id()); if(it==pending_.end()||it->second.version()!=l.version()) return {grpc::StatusCode::FAILED_PRECONDITION,"unknown write"}; files_[l.name()]=l; pending_.erase(it); save(); return grpc::Status::OK;
  }
  grpc::Status GetLayout(grpc::ServerContext*,const pv::GetLayoutRequest*r,pv::FileLayout*out) override { std::lock_guard lk(mu_); if(!valid(r->name(),r->token(),pv::TOKEN_MODE_READ)) return denied(); auto it=files_.find(r->name()); if(it==files_.end())return {grpc::StatusCode::NOT_FOUND,"file not found"}; *out=it->second; return grpc::Status::OK; }
  grpc::Status DeleteFile(grpc::ServerContext*,const pv::DeleteFileRequest*r,pv::FileLayout*out) override { std::lock_guard lk(mu_); if(!valid(r->name(),r->token(),pv::TOKEN_MODE_WRITE))return denied(); auto it=files_.find(r->name()); if(it==files_.end())return {grpc::StatusCode::NOT_FOUND,"file not found"}; *out=it->second; files_.erase(it); save(); return grpc::Status::OK; }
  grpc::Status ListFiles(grpc::ServerContext*,const pv::Empty*,pv::ListFilesResponse*out) override { std::lock_guard lk(mu_); std::vector<std::string> names; for(auto&[n,_]:files_)names.push_back(n); std::sort(names.begin(),names.end()); for(auto&n:names){auto&l=files_[n];auto*f=out->add_files();f->set_name(n);f->set_size(l.size());f->set_version(l.version());f->set_stripe_count(l.stripes_size());} return grpc::Status::OK; }
  grpc::Status Health(grpc::ServerContext*,const pv::Empty*,pv::HealthResponse*out) override {out->set_serving(true);out->set_component("coordinator");return grpc::Status::OK;}
 private:
  void purge(const std::string& f){auto&v=tokens_[f];auto now=pfs::now_ms();std::erase_if(v,[&](auto&h){return h.expiry<=now;});}
  bool valid(const std::string& f,const std::string&t,pv::TokenMode want){purge(f);for(auto&h:tokens_[f])if(h.value==t&&(h.mode==want||(h.mode==pv::TOKEN_MODE_WRITE&&want==pv::TOKEN_MODE_READ)))return true;return false;}
  static grpc::Status denied(){return {grpc::StatusCode::PERMISSION_DENIED,"valid token required"};}
  void save(){if(state_file_.empty())return;pv::MetadataSnapshot s;std::vector<std::string>names;for(auto&[n,_]:files_)names.push_back(n);std::sort(names.begin(),names.end());for(auto&n:names)*s.add_files()=files_[n];std::string bytes;if(!s.SerializeToString(&bytes))throw std::runtime_error("cannot serialize metadata");pfs::atomic_write(state_file_,bytes);}
  std::mutex mu_;std::condition_variable cv_;std::unordered_map<std::string,NodeRec>nodes_;std::unordered_map<std::string,std::vector<Held>>tokens_;std::unordered_map<std::string,pv::FileLayout>files_,pending_;std::unordered_map<std::string,uint64_t>versions_;
  std::string state_file_;
};
int main(int argc,char**argv){std::string listen=argc>1?argv[1]:"0.0.0.0:50051";std::string state=argc>2?argv[2]:"./metadata.pb";try{CoordinatorService svc(state);grpc::ServerBuilder b;b.AddListeningPort(listen,grpc::InsecureServerCredentials());b.RegisterService(&svc);auto s=b.BuildAndStart();if(!s){std::cerr<<"failed to listen\n";return 1;}std::cout<<"coordinator listening on "<<listen<<'\n';s->Wait();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
