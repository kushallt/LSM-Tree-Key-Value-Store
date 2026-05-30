#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include<fstream>
#include <algorithm>
#include <random>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
#include "filecounter.h"

float getZeroToOne();

struct Entry
{
    std::string key;
    std::string value;
    bool tombStone;

    Entry(std::string key, std::string value);
    Entry(std::string key, std::string value, int tomb);
};

struct Element
{
    std::unique_ptr<Entry> entry;
    std::vector<Element *> next;
};

class MemTable
{
public:
    int maxlevel;
    float p;
    int level;
    int nkeys = 0;

    uint64_t size = 0;
    uint64_t maxsize = 2000;
    Element *head;
    std::string wal;

    std::ofstream walstream;

    MemTable();
    ~MemTable();

    void insertElement(std::string key, std::string newvalue, int tombStone=0, int writetowal=1);
    std::vector<Element *> searchTraversal(std::string key);
    void deleteElement(std::string key);
    uint64_t calculateSize();
    std::string getElement(std::string key);
    bool writeWAL(std::string key, std::string value, uint8_t tombstone);
    bool isFileEmpty(const std::string& filename);
    bool WALinitialize();
    bool isEmpty();
    void WALtoMem();

};