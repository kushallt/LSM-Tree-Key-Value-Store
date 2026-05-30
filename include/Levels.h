#pragma once

#include "SSTable.h"
#include "filecounter.h"
#include <queue>
#include <fstream>

struct heapNode{
    std::string key;
    std::string value;
    uint8_t lsmlevel;
    uint8_t tombstone;
    uint64_t fileid;
    bool operator>(const heapNode& other) const {
        if (this->key == other.key) {
            return this->fileid < other.fileid;
        }

        return this->key > other.key;
    }

    heapNode() = default;
    heapNode(std::string key, std::string value, uint8_t tombstone, uint64_t fileid, uint8_t lsmlevel) : key(key), value(value), tombstone(tombstone), fileid(fileid), lsmlevel(lsmlevel) {}

};

class Levels{
public:
    std::vector<std::vector<std::shared_ptr<SSTable>>> levelsInfo;
    std::uint8_t maxlevels = 5;
    std::vector<uint64_t> sizeofLevels;
    std::vector<uint64_t> maxSize;
    std::string manifest = "manifest";
    std::ofstream manifeststream;
    bool initialize();
    std::string searchLevels(std::string key);
    void compact(int l1);
    void SSTtoPq(std::shared_ptr<SSTable> s, std::priority_queue<heapNode, std::vector<heapNode>, std::greater<heapNode>>& entrypq, uint8_t lsmlevel);
    void addSST(std::shared_ptr<SSTable> s);
};