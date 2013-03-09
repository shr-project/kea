// Copyright (C) 2013  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <util/memory_segment_mapped.h>

#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/offset_ptr.hpp>

#include <cassert>
#include <string>
#include <new>

using boost::interprocess::managed_mapped_file;
using boost::interprocess::open_or_create;
using boost::interprocess::open_only;
using boost::interprocess::open_read_only;
using boost::interprocess::offset_ptr;

namespace isc {
namespace util {

struct MemorySegmentMapped::Impl {
    Impl(const std::string& filename, size_t initial_size) :
        filename_(filename),
        base_sgmt_(new managed_mapped_file(open_or_create, filename.c_str(),
                                           initial_size))
    {}

    Impl(const std::string& filename, bool read_only) :
        filename_(filename),
        base_sgmt_(read_only ?
                   new managed_mapped_file(open_read_only, filename.c_str()) :
                   new managed_mapped_file(open_only, filename.c_str()))
    {}

    // mapped file; remember it in case we need to grow it.
    const std::string filename_;

    // actual Boost implementation of mapped segment.
    boost::scoped_ptr<managed_mapped_file> base_sgmt_;
};

MemorySegmentMapped::MemorySegmentMapped(const std::string& filename) :
    impl_(0)
{
    try {
        impl_ = new Impl(filename, true);
    } catch (const boost::interprocess::interprocess_exception& ex) {
        isc_throw(MemorySegmentOpenError,
                  "failed to open mapped memory segment for " << filename
                  << ": " << ex.what());
    }
}

MemorySegmentMapped::MemorySegmentMapped(const std::string& filename,
                                         bool create, size_t initial_size) :
    impl_(0)
{
    try {
        if (create) {
            impl_ = new Impl(filename, initial_size);
        } else {
            impl_ = new Impl(filename, false);
        }
    } catch (const boost::interprocess::interprocess_exception& ex) {
        isc_throw(MemorySegmentOpenError,
                  "failed to open mapped memory segment for " << filename
                  << ": " << ex.what());
    }
}

MemorySegmentMapped::~MemorySegmentMapped() {
    delete impl_;
}

void*
MemorySegmentMapped::allocate(size_t size) {
    // We explicitly check the free memory size; it appears
    // managed_mapped_file::allocate() could incorrectly return a seemingly
    // valid pointer for some very large requested size.
    if (impl_->base_sgmt_->get_free_memory() >= size) {
        void* ptr = impl_->base_sgmt_->allocate(size, std::nothrow);
        if (ptr) {
            return (ptr);
        }
    }

    // Grow the mapped segment doubling the size until we have sufficient
    // free memory in the revised segment for the requested size.
    do {
        // We first need to unmap it before calling grow().
        const size_t prev_size = impl_->base_sgmt_->get_size();
        impl_->base_sgmt_.reset();

        const size_t new_size = prev_size * 2;
        assert(new_size != 0); // assume grow fails before size overflow

        if (!managed_mapped_file::grow(impl_->filename_.c_str(),
                                       new_size - prev_size))
        {
            throw std::bad_alloc();
        }

        try {
            // Remap the grown file; this should succeed, but it's not 100%
            // guaranteed.  If it fails we treat it as if we fail to create
            // the new segment.
            impl_->base_sgmt_.reset(
                new managed_mapped_file(open_only, impl_->filename_.c_str()));
        } catch (const boost::interprocess::interprocess_exception& ex) {
            throw std::bad_alloc();
        }
    } while (impl_->base_sgmt_->get_free_memory() < size);
    isc_throw(MemorySegmentGrown, "mapped memory segment grown, size: "
              << impl_->base_sgmt_->get_size() << ", free size: "
              << impl_->base_sgmt_->get_free_memory());
}

void
MemorySegmentMapped::deallocate(void* ptr, size_t /*size*/) {
    impl_->base_sgmt_->deallocate(ptr);
}

bool
MemorySegmentMapped::allMemoryDeallocated() const {
    return (impl_->base_sgmt_->all_memory_deallocated());
}

void*
MemorySegmentMapped::getNamedAddress(const char* name) {
    offset_ptr<void>* storage =
        impl_->base_sgmt_->find<offset_ptr<void> >(name).first;
    if (storage) {
        return (storage->get());
    }
    return (0);
}

void
MemorySegmentMapped::setNamedAddress(const char* name, void* addr) {
    if (addr && !impl_->base_sgmt_->belongs_to_segment(addr)) {
        isc_throw(MemorySegmentError, "out of segment address: " << addr);
    }
    offset_ptr<void>* storage =
        impl_->base_sgmt_->find_or_construct<offset_ptr<void> >(name)();
    *storage = addr;
}

bool
MemorySegmentMapped::clearNamedAddress(const char* name) {
    return (impl_->base_sgmt_->destroy<offset_ptr<void> >(name));
}

size_t
MemorySegmentMapped::getSize() const {
    return (impl_->base_sgmt_->get_size());
}

} // namespace util
} // namespace isc
