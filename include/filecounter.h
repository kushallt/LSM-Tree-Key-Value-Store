#pragma once
#include<string>
#include <iomanip>
#include <sstream>

class FileCounter{
public:
    int currcount=1;

    int setcount(){
        return currcount++;
    }

    std::string setfilename(std::string filetype){
        std::ostringstream oss;
        oss << std::setw(6) << std::setfill('0') << currcount << ("."+filetype);
        return oss.str();
    }
};

inline FileCounter filecounter;