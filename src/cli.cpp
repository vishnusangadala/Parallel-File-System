#include "pfs/client.hpp"
#include "pfs/util.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char**argv){if(argc<3){std::cerr<<"usage: pfsctl COORDINATOR <put LOCAL REMOTE|get REMOTE LOCAL|rm REMOTE|ls>\n";return 2;}try{pfs::Client c(argv[1]);std::string cmd=argv[2];if(cmd=="put"&&argc==5)c.write(argv[4],pfs::read_all(argv[3]));else if(cmd=="get"&&argc==5)pfs::atomic_write(argv[4],c.read(argv[3]));else if(cmd=="rm"&&argc==4)c.remove(argv[3]);else if(cmd=="ls"&&argc==3)for(auto&f:c.list())std::cout<<f.name()<<'\t'<<f.size()<<'\t'<<f.stripe_count()<<" stripes\tv"<<f.version()<<'\n';else{std::cerr<<"invalid arguments\n";return 2;}}catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
