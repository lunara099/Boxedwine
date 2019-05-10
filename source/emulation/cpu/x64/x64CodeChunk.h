#ifndef __X64CODECHUNK_H__
#define __X64CODECHUNK_H__

#ifdef BOXEDWINE_X64
class x64CPU;

class X64CodeChunkLink {
public:
    static X64CodeChunkLink* alloc();

    X64CodeChunkLink() : linkTo(this), linkFrom(this) {}
    void dealloc();

    // will address to the middle of the instruction
    U32 fromEip;
    void* fromHost;

    // will point to the start of the instruction
    U32 toEip;
    void* toHost;

    KListNode<X64CodeChunkLink*> linkTo;
    KListNode<X64CodeChunkLink*> linkFrom;
};

class X64CodeChunk {
public:
    static X64CodeChunk* allocChunk(x64CPU* cpu, U32 instructionCount, U32* eipInstructionAddress, U32* hostInstructionIndex, U8* hostInstructionBuffer, U32 hostInstructionBufferLen, U32 eip, U32 eipLen);

    void dealloc();    
    void updateStartingAtHostAddress(void* hostAddress);
    void patch(U32 eip, U32 len);

    U32 getEipThatContainsHostAddress(void* hostAddress, void** startOfHostInstruction);

    void* getHostAddress() {return this->hostAddress;}
    U32 getHostAddressLen() {return this->hostLen;}

    bool containsHostAddress(void* hostAddress) {return hostAddress>=this->hostAddress && hostAddress<=(U8*)this->hostAddress+this->hostLen;}
    bool containsEip(U32 eip) {return eip>=this->emulatedAddress && eip<=this->emulatedAddress+this->emulatedLen;}
    X64CodeChunk* getNext() {return this->next;}

    void setNext(X64CodeChunk* n) {this->next = n; if (n) n->prev=this;}
    void removeFromList();

    void addLinkFrom(X64CodeChunk* from);
private:
    U32 getStartOfInstructionByEip(U32 eip, U8** hostAddress, U32* index);

    x64CPU* cpu;

    U32 emulatedAddress;
    U32 emulatedLen;
    U8* emulatedInstructionLen; // must be 15 or less per op

    void* hostAddress;
    U32 hostAddressSize;
    U32 hostLen;
    U32* hostInstructionLen;

    U32 instructionCount;
    
    // double linked list for chunks in the page
    X64CodeChunk* next;
    X64CodeChunk* prev;

    KList<X64CodeChunkLink*> linksTo;
    KList<X64CodeChunkLink*> linksFrom;
};

#endif

#endif