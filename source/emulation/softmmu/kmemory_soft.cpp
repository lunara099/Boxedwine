#include "boxedwine.h"

#ifdef BOXEDWINE_DEFAULT_MMU

#include "soft_invalid_page.h"
#include "kmemory_soft.h"
#include "soft_native_page.h"
#include "soft_ro_page.h"
#include "soft_rw_page.h"
#include "soft_wo_page.h"
#include "soft_no_page.h"
#include "soft_copy_on_write_page.h"
#include "soft_file_map.h"
#include "soft_ondemand_page.h"
#include "soft_code_page.h"

#include "soft_ram.h"

static InvalidPage _invalidPage;
static InvalidPage* invalidPage = &_invalidPage;
static U8* callbackRam;
static U32 callbackRamPos;

KMemoryData::KMemoryData(KMemory* memory) : memory(memory), mmuReadPtr{ 0 }, mmuWritePtr{ 0 }
{
    for (int i = 0; i < K_NUMBER_OF_PAGES; i++) {
        this->mmu[i] = invalidPage;
    }
    if(!callbackRam) {
        callbackRam = ramPageAlloc();
        addCallback(onExitSignal);
    }
    this->allocPages(nullptr, CALL_BACK_ADDRESS >> K_PAGE_SHIFT, 1, K_PROT_READ | K_PROT_EXEC, -1, 0, nullptr, &callbackRam);
}

void KMemoryData::setPage(U32 index, Page* page) {
    Page* p = this->mmu[index];
    this->mmu[index] = page;
    U32 address = index << K_PAGE_SHIFT;
    this->mmuReadPtr[index] = page->getReadPtr(address);
    this->mmuWritePtr[index] = page->getWritePtr(address, K_PAGE_SHIFT);
    if (p != page) {
        p->close();
    }
}

void KMemoryData::addCallback(OpCallback func) {
    U64 funcAddress = (U64)func;
    U8* address = callbackRam + callbackRamPos;

    *address = 0xFE;
    address++;
    *address = 0x38;
    address++;
    *address = (U8)funcAddress;
    address++;
    *address = (U8)(funcAddress >> 8);
    address++;
    *address = (U8)(funcAddress >> 16);
    address++;
    *address = (U8)(funcAddress >> 24);
    callbackRamPos += 6;
    if (sizeof(func) == 8) {
        address++;
        *address = (U8)(funcAddress >> 32);
        address++;
        *address = (U8)(funcAddress >> 40);
        address++;
        *address = (U8)(funcAddress >> 48);
        address++;
        *address = (U8)(funcAddress >> 56);
        callbackRamPos += 4;
    }
}

void KMemoryData::setPageRamWithFlags(U8* ram, U32 pageIndex, U8 flags, bool copyOnWrite) {
    Page* page = mmu[pageIndex];
    page->flags = flags;
    bool read = page->canRead() || page->canExec();
    bool write = page->canWrite();

    if (copyOnWrite) {
        setPage(pageIndex, CopyOnWritePage::alloc(this, ram, pageIndex << K_PAGE_SHIFT, page->flags));
    } else if (read && write) {
        setPage(pageIndex, RWPage::alloc(this, ram, pageIndex << K_PAGE_SHIFT, page->flags));
    } else if (write) {
        setPage(pageIndex, WOPage::alloc(this, ram, pageIndex << K_PAGE_SHIFT, page->flags));
    } else if (read) {
        setPage(pageIndex, ROPage::alloc(this, ram, pageIndex << K_PAGE_SHIFT, page->flags));
    } else {
        setPage(pageIndex, NOPage::alloc(this, ram, pageIndex << K_PAGE_SHIFT, page->flags));
    }
}

bool KMemoryData::isPageMapped(U32 page) {
    return (this->getPage(page)->flags & PAGE_MAPPED) != 0;
}

void KMemoryData::allocPages(KThread* thread, U32 page, U32 pageCount, U8 permissions, FD fd, U64 offset, const BoxedPtr<MappedFile>& mappedFile, U8** ramPages) {
    if (!page) {
        reserveAddress(0, pageCount, &page, false, false, PAGE_MAPPED);
    }
    if (ramPages) {
        bool read = permissions & K_PROT_READ;
        bool write = permissions & K_PROT_WRITE;

        if (read && write) {
            for (U32 i = 0; i < pageCount; i++) {
                this->setPage(page + i, RWPage::alloc(this, ramPages[i], page << K_PAGE_SHIFT, permissions));
            }
        } else if (write) {
            for (U32 i = 0; i < pageCount; i++) {
                this->setPage(page + i, WOPage::alloc(this, ramPages[i], page << K_PAGE_SHIFT, permissions));
            }
        } else if (read) {
            for (U32 i = 0; i < pageCount; i++) {
                this->setPage(page + i, ROPage::alloc(this, ramPages[i], page << K_PAGE_SHIFT, permissions));
            }
        } else {
            for (U32 i = 0; i < pageCount; i++) {
                this->setPage(page + i, NOPage::alloc(this, ramPages[i], page << K_PAGE_SHIFT, permissions));
            }
        }
    } else if (mappedFile) {
        U32 filePage = (U32)(offset >> K_PAGE_SHIFT);

        if (offset & K_PAGE_MASK) {
            kpanic("mmap: wasn't expecting the offset to be in the middle of a page");
        }

        for (U32 i = 0; i < pageCount; i++) {
            this->setPage(page + i, FilePage::alloc(this, mappedFile, filePage++, permissions));
        }
    } else {
        for (U32 i = 0; i < pageCount; i++) {
            this->setPage(page + i, OnDemandPage::alloc(this, permissions));
        }
    }
}

bool isAlignedNativePage(U32 page) {
    return (page & ~(K_NATIVE_PAGES_PER_PAGE - 1)) == page;
}

bool KMemoryData::reserveAddress(U32 startingPage, U32 pageCount, U32* result, bool canBeReMapped, bool alignNative, U32 reservedFlag) {
    U32 i;

    for (i = startingPage; i < K_NUMBER_OF_PAGES; i++) {
        if (alignNative && !isAlignedNativePage(i)) {
            continue;
        }
        if (i + pageCount >= K_NUMBER_OF_PAGES) {
            return false;
        }
        if (this->getPage(i)->type == Page::Type::Invalid_Page || (canBeReMapped && (this->getPage(i)->flags & PAGE_MAPPED))) {
            U32 j;
            bool success = true;

            for (j = 1; j < pageCount; j++) {
                U32 nextPage = i + j; // could be done a different way, but this helps the static analysis
                if (nextPage < K_NUMBER_OF_PAGES && this->getPage(nextPage)->type != Page::Type::Invalid_Page && (!canBeReMapped || !(this->getPage(i)->flags & PAGE_MAPPED))) {
                    success = false;
                    break;
                }
            }
            if (success && startingPage < ADDRESS_PROCESS_MMAP_START && i >= ADDRESS_PROCESS_MMAP_START) {
                success = false; // don't allow user app to allocate in space we reserve for kernel space
                break;
            }
            if (success) {                
                *result = i;
                for (U32 pageIndex = 0; j < pageCount; pageIndex++) {
                    mmu[i + pageIndex]->flags |= reservedFlag;
                }
                return true;
            }
            i += j; // no reason to check all the pages again
        }
    }
    return false;
}

void KMemoryData::protectPage(KThread* thread, U32 i, U32 permissions) {
    Page* page = this->getPage(i);

    U32 flags = page->flags;
    flags &= ~PAGE_PERMISSION_MASK;
    flags |= (permissions & PAGE_PERMISSION_MASK);

    // we didn't have ram backing before, so lets delay its creation
    if (page->type == Page::Type::Invalid_Page) {
        // I don't think this path is possible, we can only protect pages that are already mapped
        this->setPage(i, OnDemandPage::alloc(this, flags));
    }
    // the page type doesn't need to change, so just update flags
    else if (page->type == Page::Type::File_Page || page->type == Page::Type::On_Demand_Page || page->type == Page::Type::Copy_On_Write_Page || page->type == Page::Type::Code_Page) {
        page->flags = flags;
    }
    // we already have ram backing, so we need to preserve it and maybe change the page object
    else if (page->type == Page::Type::RO_Page || page->type == Page::Type::RW_Page || page->type == Page::Type::WO_Page || page->type == Page::Type::NO_Page) {
        RWPage* p = (RWPage*)page;

        if ((permissions & PAGE_READ) && (permissions & PAGE_WRITE)) {
            if (page->type != Page::Type::RW_Page) {
                this->setPage(i, RWPage::alloc(this, p->page, p->address, flags));
            }
        } else if (permissions & PAGE_WRITE) {
            if (page->type != Page::Type::WO_Page) {
                this->setPage(i, WOPage::alloc(this, p->page, p->address, flags));
            }
        } else if (permissions & PAGE_READ) {
            if (page->type != Page::Type::RO_Page) {
                this->setPage(i, ROPage::alloc(this, p->page, p->address, flags));
            }
        } else {
            if (page->type != Page::Type::NO_Page) {
                this->setPage(i, NOPage::alloc(this, p->page, p->address, flags));
            }
        }
    } else {
        kdebug("Memory::protect didn't expect page type: %d", page->type);
    }
}

bool KMemoryData::isPageAllocated(U32 page) {
    return this->getPage(page)->type != Page::Type::Invalid_Page;
}

U32 KMemoryData::getPageFlags(U32 page) {
    return this->mmu[page]->flags;
}

void KMemoryData::setPagesInvalid(U32 page, U32 pageCount) {
    for (U32 i = page; i < page + pageCount; i++) {
        this->setPage(i, invalidPage);
    }
}

void KMemoryData::execvReset() {
    setPagesInvalid(0, K_NUMBER_OF_PAGES);
    this->allocPages(nullptr, CALL_BACK_ADDRESS >> K_PAGE_SHIFT, 1, K_PROT_READ | K_PROT_EXEC, -1, 0, nullptr, &callbackRam);
}

U64 KMemory::readq(U32 address) {
#ifndef UNALIGNED_MEMORY
    if ((address & 0xFFF) < 0xFF9) {
        int index = address >> 12;
        if (data->mmuReadPtr[index]) {
            return *(U64*)(&data->mmuReadPtr[index][address & 0xFFF]);
        }
    }
#endif
    return readd(address) | ((U64)readd(address + 4) << 32);
}

U32 KMemory::readd(U32 address) {
    if ((address & 0xFFF) < 0xFFD) {
        int index = address >> 12;
#ifndef UNALIGNED_MEMORY
        if (data->mmuReadPtr[index])
            return *(U32*)(&data->mmuReadPtr[index][address & 0xFFF]);
#endif
        return data->mmu[index]->readd(address);
    } else {
        return readb(address) | (readb(address + 1) << 8) | (readb(address + 2) << 16) | (readb(address + 3) << 24);
    }
}

U16 KMemory::readw(U32 address) {
    if ((address & 0xFFF) < 0xFFF) {
        int index = address >> 12;
#ifndef UNALIGNED_MEMORY
        if (data->mmuReadPtr[index])
            return *(U16*)(&data->mmuReadPtr[index][address & 0xFFF]);
#endif
        return data->mmu[index]->readw(address);
    }
    return readb(address) | (readb(address + 1) << 8);
}

U8 KMemory::readb(U32 address) {
    int index = address >> 12;
    if (data->mmuReadPtr[index])
        return data->mmuReadPtr[index][address & 0xFFF];
    return data->mmu[index]->readb(address);
}

void KMemory::writeq(U32 address, U64 value) {
#ifndef UNALIGNED_MEMORY
    if ((address & 0xFFF) < 0xFF9) {
        int index = address >> 12;
        if (data->mmuWritePtr[index]) {
            *(U64*)(&data->mmuWritePtr[index][address & 0xFFF]) = value;
            return;
        }
    }
#endif
    writed(address, (U32)value); writed(address + 4, (U32)(value >> 32));
}

void KMemory::writed(U32 address, U32 value) {
    if ((address & 0xFFF) < 0xFFD) {
        int index = address >> 12;
#ifndef UNALIGNED_MEMORY
        if (data->mmuWritePtr[index])
            *(U32*)(&data->mmuWritePtr[index][address & 0xFFF]) = value;
        else
#endif
            data->mmu[index]->writed(address, value);
    } else {
        writeb(address, value);
        writeb(address + 1, value >> 8);
        writeb(address + 2, value >> 16);
        writeb(address + 3, value >> 24);
    }
}

void KMemory::writew(U32 address, U16 value) {
    if ((address & 0xFFF) < 0xFFF) {
        int index = address >> 12;
#ifndef UNALIGNED_MEMORY
        if (data->mmuWritePtr[index])
            *(U16*)(&data->mmuWritePtr[index][address & 0xFFF]) = value;
        else
#endif
            data->mmu[index]->writew(address, value);
    } else {
        writeb(address, (U8)value);
        writeb(address + 1, (U8)(value >> 8));
    }
}

void KMemory::writeb(U32 address, U8 value) {
    int index = address >> 12;
    if (data->mmuWritePtr[index])
        data->mmuWritePtr[index][address & 0xFFF] = value;
    else
        data->mmu[index]->writeb(address, value);
}

// used by futex, may point to shared memory
U8* KMemory::getIntPtr(U32 address) {
    U32 index = address >> K_PAGE_SHIFT;
    U32 offset = address & K_PAGE_MASK;
    return data->mmu[index]->getReadPtr(address, true) + offset;
}

void KMemory::clone(KMemory* from) {
    for (int i = 0; i < 0x100000; i++) {
        Page* page = from->data->getPage(i);
        if (page->type == Page::Type::On_Demand_Page) {
            if (page->mapShared()) {
                OnDemandPage* p = (OnDemandPage*)page;
                p->ondemmand(i << K_PAGE_SHIFT);
                // fall through
            } else {
                data->setPage(i, OnDemandPage::alloc(data, page->flags));
                continue;
            }
        }
        if (page->type == Page::Type::File_Page) {
            FilePage* p = (FilePage*)page;
            if (page->mapShared()) {
                p->ondemmandFile(i << K_PAGE_SHIFT);
                // fall through
            } else {
                data->setPage(i, FilePage::alloc(data, p->mapped, p->index, p->flags));
                continue;
            }
        }
        page = from->data->getPage(i); // above code could have changed this
        if (page->type == Page::Type::RO_Page || page->type == Page::Type::RW_Page || page->type == Page::Type::WO_Page || page->type == Page::Type::NO_Page) {
            RWPage* p = (RWPage*)page;
            if (!page->mapShared()) {
                data->setPage(i, CopyOnWritePage::alloc(data, p->page, p->address, p->flags));
                from->data->setPage(i, CopyOnWritePage::alloc(from->data, p->page, p->address, p->flags));
            } else {
                if (page->type == Page::Type::RO_Page) {
                    data->setPage(i, ROPage::alloc(data, p->page, p->address, p->flags));
                } else if (page->type == Page::Type::RW_Page) {
                    data->setPage(i, RWPage::alloc(data, p->page, p->address, p->flags));
                } else if (page->type == Page::Type::WO_Page) {
                    data->setPage(i, WOPage::alloc(data, p->page, p->address, p->flags));
                } else if (page->type == Page::Type::NO_Page) {
                    data->setPage(i, NOPage::alloc(data, p->page, p->address, p->flags));
                }
            }
        } else if (page->type == Page::Type::Code_Page) {
            // CodePage will check copy on write
            CodePage* p = (CodePage*)page;
            data->setPage(i, CodePage::alloc(data, p->page, p->address, p->flags));
        }  else if (page->type == Page::Type::Copy_On_Write_Page) {
            CopyOnWritePage* p = (CopyOnWritePage*)page;
            data->setPage(i, CopyOnWritePage::alloc(data, p->page, p->address, p->flags));
        } else if (page->type == Page::Type::Native_Page) {
            NativePage* p = (NativePage*)page;
            data->setPage(i, NativePage::alloc(data, p->nativeAddress, p->address, p->flags));
        } else if (page->type == Page::Type::Invalid_Page) {
            data->setPage(i, from->data->getPage(i));
        } else {
            kpanic("unhandled case when cloning memory: page type = %d", page->type);
        }
    }
}

// normal core
DecodedBlock* KMemory::getCodeBlock(U32 address) {
    Page* page = data->getPage(address >> K_PAGE_SHIFT);
    if (page->type == Page::Type::Code_Page) {
        CodePage* codePage = (CodePage*)page;
        return codePage->getCode(address);
    }
    return NULL;
}

void KMemory::addCodeBlock(U32 address, DecodedBlock* block) {
    Page* page = data->getPage(address >> K_PAGE_SHIFT);

    CodePage* codePage;
    if (page->type == Page::Type::Code_Page) {
        codePage = (CodePage*)page;
    } else {
        if (page->type == Page::Type::RO_Page || page->type == Page::Type::RW_Page || page->type == Page::Type::Copy_On_Write_Page) {
            RWPage* p = (RWPage*)page;
            codePage = CodePage::alloc(data, p->page, p->address, p->flags);
            data->setPage(address >> K_PAGE_SHIFT, codePage);
        } else {
            kpanic("Unhandled code caching page type: %d", page->type);
            codePage = nullptr;
        }
    }
    codePage->addCode(address, block, block->bytes);
}

void KMemory::logPageFault(KThread* thread, U32 address) {
    U32 start = 0;
    U32 i;
    CPU* cpu = thread->cpu;

    BString name = process->getModuleName(cpu->seg[CS].address + cpu->eip.u32);
    klog("%.8X EAX=%.8X ECX=%.8X EDX=%.8X EBX=%.8X ESP=%.8X EBP=%.8X ESI=%.8X EDI=%.8X %s at %.8X", cpu->seg[CS].address + cpu->eip.u32, cpu->reg[0].u32, cpu->reg[1].u32, cpu->reg[2].u32, cpu->reg[3].u32, cpu->reg[4].u32, cpu->reg[5].u32, cpu->reg[6].u32, cpu->reg[7].u32, name.c_str(), process->getModuleEip(cpu->seg[CS].address + cpu->eip.u32));

    klog("Page Fault at %.8X", address);
    klog("Valid address ranges:");
    for (i = 0; i < K_NUMBER_OF_PAGES; i++) {
        if (!start) {
            if (data->getPage(i) != invalidPage) {
                start = i;
            }
        } else {
            if (data->getPage(i) == invalidPage) {
                klog("    %.8X - %.8X", start * K_PAGE_SIZE, i * K_PAGE_SIZE);
                start = 0;
            }
        }
    }
    klog("Mapped Files:");
    process->printMappedFiles();
    cpu->walkStack(cpu->eip.u32, EBP, 2);
    kpanic("pf");
}

void KMemory::performOnMemory(U32 address, U32 len, bool readOnly, std::function<bool(U8* ram, U32 len)> callback) {
    if (!len) {
        return;
    }
    U32 pageIndex = address >> K_PAGE_SHIFT;
    Page* page = data->getPage(pageIndex);
    U32 offset = address & K_PAGE_MASK;
    U32 todo = len;
    if (todo > K_PAGE_SIZE - offset) {
        todo = K_PAGE_SIZE - offset;
    }

    U8* ram = readOnly ? page->getReadPtr(address, true) : page->getWritePtr(address, todo, true);
    if (!ram) {
        int ii = 0;
    }
    if (!callback(ram+offset, todo)) {
        return;
    }
    address += todo;
    len -= todo;

    while (len > K_PAGE_SIZE) {
        pageIndex++;
        page = data->getPage(pageIndex);
        ram = readOnly ? page->getReadPtr(address, true) : page->getWritePtr(address, K_PAGE_SIZE, true);
        if (!ram) {
            int ii = 0;
        }
        if (!callback(ram, K_PAGE_SIZE)) {
            return;
        }
        U32* i = (U32*)&ram[0xf2c];
        address += K_PAGE_SIZE;
        len -= K_PAGE_SIZE;
    }

    if (len > 0) {
        pageIndex++;
        page = data->getPage(pageIndex);
        ram = readOnly ? page->getReadPtr(address, true) : page->getWritePtr(address, len, true);
        if (!ram) {
            int ii = 0;
        }
        callback(ram, len);
    }
}

// This doesn't mean the data can't be changed by another thread, it just means the pointer will stay valid
U8* KMemory::lockReadOnlyMemory(U32 address, U32 len) {
    U32 pageIndex = address >> K_PAGE_SHIFT;
    Page* page = data->getPage(pageIndex);
    U32 offset = address & K_PAGE_MASK;

    // if we cross a page boundry then we will need to make a copy
    if (len <= K_PAGE_SIZE - offset) {
        return page->getReadPtr(address, true);
    }

    kpanic("KMemory::lockReadOnlyMemory need to implement");
    return nullptr;
}

void KMemory::unlockMemory(U8 * lockedPointer) {
    // :TODO: nothing todo until lockReadOnlyMemory supports crossing page boundry
}

#endif