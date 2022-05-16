/*
  ISC License

  Copyright (c) 2022, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#pragma once

#include "fuse_rename_policy_base.hpp"
#include "fuse_rename_policy_factory.hpp"

#include "toml.hpp"


namespace FUSE::RENAME
{
  class Policy
  {
  public:
    Policy(const toml::value &toml_)
    {
      _rename = POLICY::factory(toml_);
    }

  public:
    int
    operator()(const gfs::path &oldpath_,
               const gfs::path &newpath_)
    {
      return (*_rename)(oldpath_,newpath_);
    }

  private:
    POLICY::Base::Ptr _rename;
  };
}
