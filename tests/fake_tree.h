// SPDX-License-Identifier: Apache-2.0
// A throwaway directory tree standing in for /sys and /proc. Created under
// the test's working directory (meson's build tree), removed afterwards.
#pragma once

#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <ftw.h>
#include <unistd.h>

#include "util.h"

class FakeTree {
public:
    FakeTree()
    {
        char tmpl[] = "fake-tree-XXXXXX";
        char *d = mkdtemp(tmpl);
        char cwd[4096];
        root_ = std::string(getcwd(cwd, sizeof(cwd)) ? cwd : ".") + "/" +
                (d ? d : "fake-tree-failed");
        util_set_root(root_.c_str());
    }
    ~FakeTree()
    {
        util_set_root("");
        nftw(root_.c_str(), rm, 16, FTW_DEPTH | FTW_PHYS);
    }
    FakeTree(const FakeTree &) = delete;
    FakeTree &operator=(const FakeTree &) = delete;

    // Write path (absolute, as on the device) with contents, creating parents.
    void write(const std::string &path, const std::string &contents)
    {
        mkdirs(path.substr(0, path.rfind('/')));
        std::ofstream(root_ + path) << contents;
    }
    void mkdir(const std::string &path) { mkdirs(path); }
    std::string read(const std::string &path) const
    {
        std::ifstream f(root_ + path);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    const std::string &root() const { return root_; }

private:
    void mkdirs(const std::string &path)
    {
        std::string cur = root_;
        size_t pos = 0;
        while (pos != std::string::npos) {
            size_t next = path.find('/', pos + 1);
            cur = root_ + path.substr(0, next);
            ::mkdir(cur.c_str(), 0755);
            pos = next;
        }
    }
    static int rm(const char *p, const struct stat *, int, struct FTW *)
    {
        return remove(p);
    }
    std::string root_;
};
