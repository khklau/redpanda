#pragma once

#include "model/record_batch_reader.h"
#include "storage/log_segment.h"
#include "storage/offset_tracker.h"
#include "storage/parser.h"
#include "utils/fragbuf.h"

#include <seastar/core/io_queue.hh>
#include <seastar/util/optimized_optional.hh>

namespace storage {

struct log_reader_config {
    model::offset start_offset;
    size_t max_bytes;
    size_t min_bytes;
    io_priority_class prio;
};

class log_segment_reader;

class skipping_consumer : public batch_consumer {
public:
    explicit skipping_consumer(log_segment_reader& reader) noexcept
      : _reader(reader) {
    }

    void set_timeout(model::timeout_clock::time_point timeout) {
        _timeout = timeout;
    }

    virtual skip consume_batch_start(
      model::record_batch_header, size_t num_records) override;

    virtual skip consume_record_key(
      size_t size_bytes,
      int32_t timestamp_delta,
      int32_t offset_delta,
      fragbuf&& key) override;

    virtual void consume_record_value(fragbuf&&) override;

    virtual void
    consume_compressed_records(fragbuf&&) override;

    virtual stop_iteration consume_batch_end() override;

private:
    log_segment_reader& _reader;
    model::offset _start_offset;
    model::record_batch_header _header;
    size_t _record_size_bytes;
    int32_t _record_timestamp_delta;
    int32_t _record_offset_delta;
    fragbuf _record_key;
    size_t _num_records;
    model::record_batch::records_type _records;
    model::timeout_clock::time_point _timeout;
};

class log_segment_reader : public model::record_batch_reader::impl {
    static constexpr size_t max_buffer_size = 8 * 1024;
    using span = model::record_batch_reader::impl::span;

public:
    log_segment_reader(
      log_segment_ptr seg,
      offset_tracker& tracker,
      log_reader_config config) noexcept;

    size_t bytes_read() const {
        return _bytes_read;
    }

protected:
    virtual future<span>
      do_load_slice(model::timeout_clock::time_point) override;

private:
    future<> initialize();

    bool is_initialized() const;

    bool is_buffer_full() const;

    /// reset_state() allows further reads to happen on a reader
    /// that previously reached the end of the stream. Useful to
    /// implement cached readers that can continue a read where
    /// it left off.
    void reset_state();

    friend class log_reader;

private:
    log_segment_ptr _seg;
    offset_tracker& _tracker;
    log_reader_config _config;
    size_t _bytes_read = 0;
    skipping_consumer _consumer;
    input_stream<char> _input;
    continuous_batch_parser_opt _parser;
    std::vector<model::record_batch> _buffer;
    size_t _buffer_size = 0;
    bool _over_committed_offset = false;

    friend class skipping_consumer;
};

class log_reader : public model::record_batch_reader::impl {
public:
    using span = model::record_batch_reader::impl::span;

    // Note: The offset tracker will contain base offsets,
    //       and will never point to an offset within a
    //       batch, that is, of an individual record. This
    //       is because batchs are atomically made visible.
    log_reader(log_set&, offset_tracker&, log_reader_config) noexcept;

    virtual future<span>
      do_load_slice(model::timeout_clock::time_point) override;

private:
    bool is_done();

    using reader_available = bool_class<struct create_reader_tag>;

    reader_available maybe_create_segment_reader();

private:
    log_segment_selector _selector;
    offset_tracker& _offset_tracker;
    log_reader_config _config;
    std::optional<log_segment_reader> _current_reader;
};

} // namespace storage