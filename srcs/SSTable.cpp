#include "SSTable.h"
SSTable::~SSTable(){
    // if(std::filesystem::exists(filename)){
    //     std::filesystem::remove(filename);
    // }
}
void SSTable::writeSST(MemTable &memtable)
{
    if (stored == 1)
    {
        std::cout << "SST already written" << std::endl;
        return;
    }
    std::ofstream outfile(filename, std::ios::binary | std::ios::trunc);

    std::vector<std::pair<uint64_t, std::string>> tempIndexList;
    uint64_t bloomfilteroffset;
    int nbuckets = memtable.nkeys * 10;
    numhashes = 7;
    bloomBuckets.assign(nbuckets, 0);

    Element *n = memtable.head->next[0];
    std::string key, value;
    uint8_t keylength, vallength;
    uint64_t offset;
    uint64_t prevoffset = -1-blocksize;

    int setstart = 0;
    uint64_t prevfilesize = getfilesize();

    while (n != nullptr)
    {
        key = n->entry->key;
        value = n->entry->value;
        uint8_t tombstone = n->entry->tombStone;
        endkey = key;

        if(!setstart){
            startkey = key;
            setstart = 1;
        }

        keylength = key.size();
        vallength = value.size();

        computeBloomFingerprint(bloomBuckets, key);

        uint64_t currfilesize = getfilesize();
        offset = outfile.tellp();
        if (offset - prevoffset > blocksize)
        {
            prevoffset = offset;
            tempIndexList.emplace_back(offset, key);
            prevfilesize = currfilesize;
        }
        outfile.write((char *)&keylength, sizeof(uint8_t));
        outfile.write(key.data(), key.size());

        outfile.write((char *)&vallength, sizeof(uint8_t));
        outfile.write(value.data(), value.size());

        outfile.write((char *)&tombstone, sizeof(uint8_t));


        n = n->next[0];
    }
    bloomfilteroffset = outfile.tellp();
    for (const auto &b : bloomBuckets)
    {
        outfile.write((char *)&b, sizeof(uint8_t));
    }
    uint64_t indexOffset = outfile.tellp();
    uint8_t strlength;
    for (const auto &p : tempIndexList)
    {
        strlength = p.second.size();
        outfile.write((char *)&strlength, sizeof(uint8_t));
        outfile.write(p.second.data(), strlength);
        outfile.write((char *)&(p.first), sizeof(uint64_t));
    }
    footerOffset = outfile.tellp();
    outfile.write((char *)&bloomfilteroffset, sizeof(bloomfilteroffset));
    outfile.write((char *)&indexOffset, sizeof(indexOffset));
    stored = 1;
    outfile.close();
    // std::cout << "done writing to sst." << std::endl;
    memtable.walstream.close();
    if(std::filesystem::exists(memtable.wal)){
        std::filesystem::remove(memtable.wal);
    }
}

void SSTable::computeBloomFingerprint(std::vector<uint8_t> &buckets, std::string key)
{
    uint64_t h1 = fnv1a(key, 0x123456789ABCDEF0ULL);
    uint64_t h2 = fnv1a(key, 0xFEDCBA9876543210ULL);
    uint64_t bucketssize = (uint64_t)(buckets.size() * 8);
    for (int i = 0; i < numhashes; i++)
    {
        
        uint64_t bucket_index = (h1 + i * h2) % bucketssize;

        size_t byte_idx = bucket_index / 8; // Division to find the byte bucket
        int bit_pos = bucket_index % 8;     // Modulo to find the bit slot inside that byte

        buckets[byte_idx] |= (1 << bit_pos);
    }
}
bool SSTable::bloomFilter(const std::string &key, const std::vector<uint8_t> &buckets)
{
    uint64_t h1 = fnv1a(key, 0x123456789ABCDEF0ULL);
    uint64_t h2 = fnv1a(key, 0xFEDCBA9876543210ULL);
    uint64_t total_bits = buckets.size() * 8;
    for (int i = 0; i < numhashes; i++)
    {

        uint64_t bucket_index = (h1 + i * h2) % total_bits;

        size_t byte_idx = bucket_index / 8;
        int bit_pos = bucket_index % 8;

        if (!(buckets[byte_idx] & (1 << bit_pos)))
        {
            return 0;
        }
    }
    
    return 1;
}

uint64_t SSTable::fnv1a(const std::string &key, uint64_t seed)
{
    uint64_t hash = seed;
    for (char c : key)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string SSTable::searchKey(std::string key)
{
    if(!bloomFilter(key, bloomBuckets)) return "";
    std::ifstream infile(filename, std::ios::binary);

    uint64_t sparseIndexOffset = 0;
    uint64_t bloomOffset = 0;
    infile.seekg(footerOffset, std::ios::beg);

    infile.read((char *)(&bloomOffset), sizeof(bloomOffset));    
    infile.read((char *)&sparseIndexOffset, sizeof(sparseIndexOffset));

    uint8_t strlength;
    uint64_t offset;
    uint64_t trueoffset;
    int flag = 0;
    std::string currentkey;

    infile.seekg(sparseIndexOffset, std::ios::beg);

    while(infile && (uint64_t)infile.tellg() < footerOffset){
        if(!infile.read((char*)&strlength, sizeof(strlength))) break;
        std::string searchkey(strlength, '\0');
        if(!infile.read(searchkey.data(), (size_t)strlength)) break;
        if(!infile.read((char *)&offset, sizeof(offset))) break;
        if(searchkey <= key){
            trueoffset = offset;
            currentkey = searchkey;
            flag = 1;
        }
        else break;
    }
    if(!flag) return "";
    infile.seekg(trueoffset, std::ios::beg);
    
    while(infile && (uint64_t)infile.tellg() < bloomOffset){
        uint8_t keylength, vallength;

        if(!infile.read((char*)&keylength, sizeof(uint8_t))) break;
        std::string currkey(keylength, '\0');
        if(!infile.read(currkey.data(), (size_t)keylength)) break;

        infile.read((char *)&vallength, sizeof(uint8_t));
        std::string currval(vallength, '\0');
        infile.read(currval.data(), (size_t)vallength);

        uint8_t tombstone;
        infile.read((char *)&tombstone, sizeof(tombstone));

        if(currkey == key){
           if(tombstone) return "[DELETED KEY]";
           else return currval;
        }
        if(currkey > key) break;
    }
    return "";

}
void SSTable::writeBuckets(std::vector<uint8_t>& buckets){
    for(const auto& b:buckets){
        std::cout<<static_cast<int>(b)<<" ";
    }
    std::cout<<std::endl;
}

uint64_t SSTable::getfilesize(){
    return (uint64_t)std::filesystem::file_size(filename);
}