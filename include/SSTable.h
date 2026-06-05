#pragma once
#include <iostream>
#include <fstream>
#include <cstdint>
#include <utility>
#include "MemTable.h"

class SSTable
{
public:
    std::string startkey;
    std::string endkey;
    std::string filename;
    uint64_t filecount;
    uint64_t filesize = 0;
    uint64_t blocksize = 20;
    int numhashes;
    uint64_t footerOffset;
    int stored = 0;
    std::vector<uint8_t> bloomBuckets;
    void writeSST(MemTable& memtable);
    void computeBloomFingerprint(std::vector<uint8_t>& buckets, std::string key);
    bool bloomFilter(const std::string &key, const std::vector<uint8_t> &buckets);
    uint64_t fnv1a(const std::string& key, uint64_t seed);
    std::string searchKey(std::string key);
    void writeBuckets(std::vector<uint8_t>& buckets);
    uint64_t getfilesize();

    SSTable() = default;
    ~SSTable();

};