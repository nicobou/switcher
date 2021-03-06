/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

#ifndef _SWITCHER_LOGGED_H_
#define _SWITCHER_LOGGED_H_

#include "./base.hpp"

#define MakeSwitcherLoggedMember(NAME)                                                          \
  template <typename... Targs>                                                                  \
  void NAME(const char* format, const ::std::string& value, Targs... Fargs) const {             \
    log_->NAME(format, std::forward<const std::string&>(value), std::forward<Targs>(Fargs)...); \
  }                                                                                             \
  void NAME(const char* format) const { log_->NAME(format); }

namespace switcher {
namespace log {

class Logged {

 protected:
  Logged() = delete;
  Logged(Base* bl) : log_(bl) {}
  MakeSwitcherLoggedMember(error);
  MakeSwitcherLoggedMember(critical);
  MakeSwitcherLoggedMember(warning);
  MakeSwitcherLoggedMember(message);
  MakeSwitcherLoggedMember(info);
  MakeSwitcherLoggedMember(debug);
  Base* get_log_ptr() { return log_; };

 private:
  mutable Base* log_{nullptr};
};

}  // namespace log
}  // namespace shmdata
#endif
