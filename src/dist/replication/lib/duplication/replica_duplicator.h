/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 *
 * -=- Robust Distributed System Nucleus (rDSN) -=-
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <dsn/dist/replication/duplication_common.h>
#include <dsn/cpp/pipeline.h>
#include <dsn/dist/replication/replica_base.h>
#include <dsn/dist/replication.h>
#include <dsn/tool-api/zlocks.h>

namespace dsn {
namespace replication {

class duplication_progress
{
public:
    // the maximum decree that's been persisted in meta server
    decree confirmed_decree{invalid_decree};

    // the maximum decree that's been duplicated to remote.
    decree last_decree{invalid_decree};

    duplication_progress &set_last_decree(decree d)
    {
        last_decree = d;
        return *this;
    }

    duplication_progress &set_confirmed_decree(decree d)
    {
        confirmed_decree = d;
        return *this;
    }
};

struct load_mutation;
struct ship_mutation;
struct load_from_private_log;
class replica;

// Each replica_duplicator is responsible for one duplication.
// It works in THREAD_POOL_REPLICATION (LPC_DUPLICATE_MUTATIONS),
// sharded by gpid, thus all functions are single-threaded,
// no read lock required (of course write lock is necessary when
// reader could be in other thread).
//
// TODO(wutao1): optimize for multi-duplication
// Currently we create duplicator for every duplication.
// They're isolated even if they share the same private log.
struct replica_duplicator : replica_base, pipeline::base
{
public:
    replica_duplicator(const duplication_entry &ent, replica *r);

    // This is a blocking call.
    // The thread may be seriously blocked under the destruction.
    // Take care when running in THREAD_POOL_REPLICATION, though
    // duplication removal is extremely rare.
    ~replica_duplicator();

    // Advance this duplication to status `next_status`.
    void update_status_if_needed(duplication_status::type next_status);

    dupid_t id() const { return _id; }

    const std::string &remote_cluster_address() const { return _remote_cluster_address; }

    // Thread-safe
    duplication_progress progress() const
    {
        zauto_read_lock l(_lock);
        return _progress;
    }

    // Thread-safe
    void update_progress(const duplication_progress &p);

    void start();

    // Holds its own tracker, so that other tasks
    // won't be effected when this duplication is removed.
    dsn::task_tracker *tracker() { return &_tracker; }

    std::string to_string() const;

    // to ensure mutation logs after start_decree is available for duplication
    error_s verify_start_decree(decree start_decree);

private:
    decree get_max_gced_decree() const;

private:
    friend struct replica_duplicator_test;
    friend struct duplication_sync_timer_test;

    friend struct load_mutation;
    friend struct ship_mutation;

    const dupid_t _id;
    const std::string _remote_cluster_address;

    perf_counter_wrapper _pending_duplicate_count;
    task_ptr _pending_duplicate_count_timer;

    replica *_replica;
    dsn::task_tracker _tracker;

    duplication_status::type _status{duplication_status::DS_INIT};

    // protect the access of _progress.
    mutable zrwlock_nr _lock;
    duplication_progress _progress;

    /// === pipeline === ///
    std::unique_ptr<load_mutation> _load;
    std::unique_ptr<ship_mutation> _ship;
    std::unique_ptr<load_from_private_log> _load_private;
};

typedef std::unique_ptr<replica_duplicator> replica_duplicator_u_ptr;

} // namespace replication
} // namespace dsn
