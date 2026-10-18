#include "pfs/client.hpp"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
int main(int argc,char**argv){std::string addr=argc>1?argv[1]:"localhost:50051";try{pfs::Client c(addr,"integration");std::string payload;for(int i=0;i<10000;++i)payload+="parallel-filesystem-test-"+std::to_string(i)+'\n';c.write("/integration/data",payload,4096);if(c.read("/integration/data")!=payload)throw std::runtime_error("round trip mismatch");std::atomic<int>ok=0;std::vector<std::thread>ts;for(int i=0;i<16;++i)ts.emplace_back([&,i]{try{pfs::Client x(addr,"reader-"+std::to_string(i));if(x.read("/integration/data")==payload)++ok;}catch(...){}});for(auto&t:ts)t.join();if(ok!=16)throw std::runtime_error("concurrent readers failed");c.remove("/integration/data");if(!c.list().empty())throw std::runtime_error("delete failed");std::cout<<"PASS round-trip, striping, 16 concurrent readers, delete\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
