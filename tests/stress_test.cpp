#include "pfs/client.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
int main(int argc,char**argv){std::string addr=argc>1?argv[1]:"localhost:50051";int clients=argc>2?std::stoi(argv[2]):210;int files=argc>3?std::stoi(argv[3]):216;std::atomic<int>ok=0,fail=0;auto start=std::chrono::steady_clock::now();std::vector<std::thread>ts;for(int i=0;i<clients;++i)ts.emplace_back([&,i]{try{pfs::Client c(addr,"stress-"+std::to_string(i));for(int j=i;j<files;j+=clients){std::string name="/stress/file-"+std::to_string(j),data(16384,char('A'+j%26));data+=std::to_string(j);c.write(name,data,4096);if(c.read(name)!=data)throw std::runtime_error("mismatch");++ok;}}catch(const std::exception&e){++fail;std::cerr<<"client failure: "<<e.what()<<'\n';}});for(auto&t:ts)t.join();auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();std::cout<<"clients="<<clients<<" files="<<files<<" successful_files="<<ok<<" failures="<<fail<<" elapsed_ms="<<ms<<" ops_per_sec="<<(ms?ok.load()*2000.0/ms:0)<<'\n';return ok==files&&fail==0?0:1;}
