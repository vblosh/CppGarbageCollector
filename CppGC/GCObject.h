#pragma once
#include <iostream>
#include <vector>

namespace cppgc
{

    // Define your container class
    class GCObject {
    public:
        virtual ~GCObject() {}

        // iterator class
        class iterator {
        private:
            using PtrCont = typename std::vector<GCObject**>::iterator;

            PtrCont current;

        public:
            iterator(PtrCont iter) : current(iter) {}

            iterator& operator++() {
                ++current;
                return *this;
            }

            GCObject** operator*() {
                return *current;
            }

            bool operator==(const iterator& other) const {
                return current == other.current;
            }

            bool operator!=(const iterator& other) const {
                return current != other.current;
            }
        };

        iterator begin() {
            return iterator(children.begin());
        }

        iterator end() {
            return iterator(children.end());
        }

        bool visited = false;

    protected:
        std::vector<GCObject**> children;
    };

}

