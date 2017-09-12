// $Id: cix.cpp,v 1.4 2016-05-09 16:01:56-07 - - $
//Marko Jerkovic (mjerkovi@ucsc.edu)
//Kevin Jacobberger (kjacobbe@ucsc.edu)

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream log (cout);
struct cix_exit: public exception {};

unordered_map<string,cix_command> command_map {
   {"exit", cix_command::EXIT},
   {"help", cix_command::HELP},
   {"ls"  , cix_command::LS  },
   {"rm"  , cix_command::RM  },
   {"put" , cix_command::PUT },
   {"get" , cix_command::GET },
};

void cix_help() {
   static const vector<string> help = {
      "exit         - Exit the program.  Equivalent to EOF.",
      "get filename - Copy remote file to local host.",
      "help         - Print help summary.",
      "ls           - List names of files on remote server.",
      "put filename - Copy local file to remote host.",
      "rm filename  - Remove file from remote server.",
   };
   for (const auto& line: help) cout << line << endl;
}

void cix_ls (client_socket& server) {
   cix_header header;
   header.command = cix_command::LS;
   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;
   if (header.command != cix_command::LSOUT) {
      log << "sent LS, server did not return LSOUT" << endl;
      log << "server returned " << header << endl;
   }else {
      char buffer[header.nbytes + 1];
      recv_packet (server, buffer, header.nbytes);
      log << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      cout << buffer;
   }
}

void cix_rm (client_socket& server, const string& fname) {
   cix_header header;
   header.command = cix_command::RM;
   //iterates over fname string and populates filename char array
   //with the file name
   for(size_t i = 0;i != fname.size();++i)
      header.filename[i] = fname[i];
   header.filename[fname.size()] = 0;
   log << "sending header " << header << endl;
   send_packet(server, &header, sizeof header);
   //either will receive a NAK or an ACK
   recv_packet(server, &header, sizeof header);
   log << "recieved header " << header <<endl;
   if (header.command != cix_command::ACK){
      log << "sent RM, server did not return ACK" << endl;
      log << fname << ": " <<strerror(header.nbytes) << endl;
   }
   else
      log << fname << " successfully removed" << endl; 
}

void cix_put (client_socket& server, const string& fname) {
   cix_header header;
   header.command = cix_command::PUT;
   for(size_t i = 0;i != fname.size();++i)
      header.filename[i] = fname[i];
   header.filename[fname.size()] = 0;
   //attempts to open fnam in current directory if file doesn't exist
   //do nothing
   ifstream in_file(fname, std::ifstream::binary);
   if(!in_file) {
      log << fname << ": "  << strerror(errno)  << endl;
      return;
   }
   //finds length of file
   in_file.seekg(0, in_file.end);
   int length = in_file.tellg();
   in_file.seekg(0);
   //creates character buffer to hold the contents of the file
   char buffer[length];
   in_file.read(buffer, length);
   in_file.close();
   header.nbytes = length;
   log << "sending header: " << header << endl;
   send_packet(server, &header, sizeof header);
   send_packet(server, buffer, length);
   log << "sent " << length << " bytes" << endl;
   recv_packet(server, &header, sizeof header);
   log << "recieved header " << header << endl; 
   if (header.command != cix_command::ACK){
      log << "sent PUT, server did not return ACK" << endl;
      log << strerror(header.nbytes) << endl;
   }
   else
      log << fname << " successfully copied to server" << endl;
}

void cix_get (client_socket& server, const string& fname) {
   cix_header header;
   header.command = cix_command::GET;
   for(size_t i = 0; i != fname.size();++i)
      header.filename[i] = fname[i];
   header.filename[fname.size()] = 0;
   log << "sending header: " << header << endl;
   send_packet(server, &header, sizeof header);
   //next packet to come is a header with the number of bytes of the
   //payload to follow
   recv_packet(server, &header, sizeof header);
   log << "received header: " << header << endl; 
   if(header.command == cix_command::NAK) {
      log << fname << ": "  <<strerror(header.nbytes) << endl;
      return;
   }
   char buffer[header.nbytes];
   //payload packet
   recv_packet(server, buffer, header.nbytes);
   ofstream out_file(fname, std::ofstream::binary);
   if(!out_file){
      log << "outfile unable to open"<<endl;//?
      return;
   }
   out_file.write(buffer, header.nbytes);
   out_file.close();
}


void usage() {
   cerr << "Usage: " << log.execname() << " [host] [port]" << endl;
   throw cix_exit();
}

vector<string> split (const string& line, const string& delimiters){
   vector<string> words;
   size_t end = 0;
   for(;;){
      size_t start = line.find_first_not_of (delimiters, end);
      if(start == string::npos) break;
      end =line.find_first_of (delimiters, start);
      words.push_back (line.substr(start, end- start));
   }
   return words;
   
}

int main (int argc, char** argv) {
   log.execname (basename (argv[0]));
   log << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cix_server_host (args, 0);
   in_port_t port = get_cix_server_port (args, 1);
   log << to_string (hostinfo()) << endl;
   try {
      log << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      log << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cix_exit();
         log << "command " << line << endl;
         vector<string> line_vec = split(line, " ");//
         const auto& itor = command_map.find (line_vec[0]);
         cix_command cmd = itor == command_map.end()
                         ? cix_command::ERROR : itor->second;
         if(line_vec.size()>2){ cmd = cix_command::ERROR; }
         switch (cmd) {
            case cix_command::EXIT:
               throw cix_exit();
               break;
            case cix_command::HELP:
               cix_help();
               break;
            case cix_command::LS:
               cix_ls (server);
               break;
            case cix_command::RM:
               cix_rm (server, line_vec[1]);
               break;
            case cix_command::PUT:
               cix_put (server, line_vec[1]);
               break;
            case cix_command::GET:
               cix_get (server, line_vec[1]);
               break;
            default:
               log << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      log << error.what() << endl;
   }catch (cix_exit& error) {
      log << "caught cix_exit" << endl;
   }
   log << "finishing" << endl;
   return 0;
}

