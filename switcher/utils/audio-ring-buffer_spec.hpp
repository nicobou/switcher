/*
 * This file is part of switcher-jack.
 *
 * switcher-myplugin is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <iostream>

namespace switcher {
namespace utils {

template <typename SampleType>
AudioRingBuffer<SampleType>::AudioRingBuffer(std::size_t size_in_sample)
    : buffer_size_(size_in_sample), buffer_(size_in_sample), available_size_(size_in_sample) {}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::put_samples(std::size_t num,
                                                     std::function<SampleType()> sample_getter) {
  std::size_t available = available_size_.load();
  std::size_t res = num;
  if (available < num) res = available;
  if (0 == res) return res;
  for (std::size_t i = 0; i < res; ++i) {
    buffer_[write_] = sample_getter();
    ++write_;
    if (buffer_size_ == write_) write_ = 0;
  }
  available_size_.fetch_sub(res);
  return res;
}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::pop_samples(std::size_t num, SampleType* dest) {
  std::size_t available = buffer_size_ - available_size_.load();
  std::size_t res = num;
  if (available < num) res = available;
  if (0 == res) return res;
  if (nullptr == dest) {
    read_ = (read_ + res) % buffer_size_;
    available_size_.fetch_add(res);
    return res;
  }
  auto first_read = buffer_size_ - read_;
  if (first_read >= num) {  // one copy is enought
    std::memcpy(static_cast<void*>(dest), buffer_.data() + read_, num * sizeof(SampleType));
    read_ += num;
  } else {  // copy the end of the buffer, then the remaining samples located at the beginning
    std::memcpy(static_cast<void*>(dest), buffer_.data() + read_, first_read * sizeof(SampleType));
    std::memcpy(static_cast<void*>(dest + first_read),
                buffer_.data(),
                (num - first_read) * sizeof(SampleType));
    read_ = num - first_read;
  }
  available_size_.fetch_add(res);
  return res;
}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::read_samples(std::size_t num, SampleType* dest) {
  std::size_t available = buffer_size_ - available_size_.load();
  std::size_t res = num;
  if (available < num) res = available;
  if (0 == res || nullptr == dest) {
    return 0;
  }
  auto first_read = buffer_size_ - read_;
  if (first_read >= num) {  // one copy is enought
    std::memcpy(static_cast<void*>(dest), buffer_.data() + read_, num * sizeof(SampleType));
  } else {  // copy the end of the buffer, then the remaining samples at the beginning
    std::memcpy(static_cast<void*>(dest), buffer_.data() + read_, first_read * sizeof(SampleType));
    std::memcpy(static_cast<void*>(dest + first_read),
                buffer_.data(),
                (num - first_read) * sizeof(SampleType));
  }
  return res;
}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::read_samples_as_channel(std::size_t num,
                                                                 SampleType* dest,
                                                                 unsigned int chan,
                                                                 unsigned int total_chan) {
  std::size_t available = buffer_size_ - available_size_.load();
  std::size_t res = num;
  if (available < num) res = available;
  if (0 == res || nullptr == dest) {
    return 0;
  }
  auto read = read_;
  for (std::size_t i = 0; i < res; ++i) {
    dest[(i * total_chan) + (chan - 1)] = buffer_[read];
    ++read;
    if (buffer_size_ == read) read = 0;
  }
  return res;
}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::pop_samples_as_channel(std::size_t num,
                                                                SampleType* dest,
                                                                unsigned int chan,
                                                                unsigned int total_chan) {
  std::size_t available = buffer_size_ - available_size_.load();
  std::size_t res = num;
  if (available < num) res = available;
  if (0 == res) return res;
  if (nullptr == dest) {
    read_ = (read_ + res) % buffer_size_;
    return res;
  }
  for (std::size_t i = 0; i < res; ++i) {
    dest[(i * total_chan) + (chan - 1)] = buffer_[read_];
    ++read_;
    if (buffer_size_ == read_) read_ = 0;
  }
  available_size_.fetch_add(res);
  return res;
}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::get_usage() {
  return buffer_size_ - available_size_.load();
}

template <typename SampleType>
std::size_t AudioRingBuffer<SampleType>::shrink_to(std::size_t size) {
  std::size_t available = buffer_size_ - available_size_.load();
  if (available < size) return 0;
  std::size_t res = available - size;
  read_ = (read_ + res) % buffer_size_;
  available_size_.fetch_add(res);
  return res;
}

}  // namespace utils
}  // namespace switcher
