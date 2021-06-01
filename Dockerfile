FROM ubuntu:20.10
RUN apt update 
RUN apt install -y build-essential bison gperf cmake libsqlite3-dev libaspell-dev libpcre3-dev nettle-dev g++ libcurl4-openssl-dev libargon2-dev

WORKDIR /toaststunt
COPY src /toaststunt/src/

COPY CMakeLists.txt  /toaststunt/
COPY CMakeModules  /toaststunt/CMakeModules


WORKDIR /toaststunt/
RUN pwd ;  mkdir build && cd build && cmake ../
RUN cd /toaststunt/build && make -j2

# A special restart which output on stdout is needed for docker
COPY docker_restart.sh /toaststunt/build/
EXPOSE 7777
ENTRYPOINT cd /toaststunt/build/ ;  ./docker_restart.sh /cores/$CORE_TO_LOAD
