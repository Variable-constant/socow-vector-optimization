#pragma once
#include <cstddef>
#include <array>
#include "storage.h"

template <typename T, size_t SMALL_SIZE>
struct socow_vector {
    using iterator = T*;
    using const_iterator = T const*;

    socow_vector() noexcept
        : size_(0), small(true) {}

    socow_vector(socow_vector const& that) {
        if (that.small) {
            static_storage = that.static_storage;
        } else {
            s = that.s;
            s->counter++;
        }
        size_ = that.size_;
        small = that.small;
    }

    socow_vector& operator=(socow_vector const& other) {
        if (this == &other) {
            return *this;
        }
        socow_vector tmp = socow_vector(other);
        tmp.swap(*this);
        return *this;
    }

    ~socow_vector() {
        if (small) {
            destruct_range(my_begin(), my_end());
        } else {
            if (s->counter == 1) {
                destruct_range(my_begin(), my_end());
                operator delete(s->data_);
                operator delete(s);
            } else {
                s->counter--;
            }
        }
        size_ = 0;
    }

    T& operator[](size_t i) noexcept {
        if (!small && s->counter > 1)
            realloc(s->capacity, my_begin(), my_end(), s);
        return *(my_begin() + i);
    }

    T const& operator[](size_t i) const noexcept {
        return *(begin() + i);
    }

    T* data() {
        if (!small && s->counter > 1)
            realloc(s->capacity, my_begin(), my_end(), s);
        return (small ? static_storage.begin() : s->data_);
    }

    T const* data() const noexcept {
        return (small ? static_storage.begin() : s->data_);
    }

    size_t size() const noexcept {
        return size_;
    }

    T& front() noexcept {
        if (!small && s->counter > 1)
            realloc(s->capacity, my_begin(), my_end(), s);
        return *my_begin();
    }

    T const& front() const noexcept {
        return *begin();
    }

    T& back() noexcept {
        if (!small && s->counter > 1)
            realloc(s->capacity, my_begin(), my_end(), s);
        return *(my_end() - 1);
    }
    T const& back() const noexcept {
        return *(end() - 1);
    }
    void push_back(T const& e) {
        if (small && size_ + 1 <= SMALL_SIZE) {
            new (my_end()) T(e);
        } else {
            if (small) {
                auto* new_st = new storage<T>;
                realloc(SMALL_SIZE * 2, my_begin(), my_end(), new_st);
                destruct_range(my_begin(), my_end());
                T tmp = e;
                s = new_st;
                small = false;
                new(my_end()) T(tmp);
            } else {
                if (s->capacity == size_ || s->counter > 1) {
                    T tmp = e;
                    realloc((s->capacity * (s->capacity == size_ ? 2 : 1)), my_begin(), my_end(), s);
                    new (my_end()) T(tmp);
                } else {
                    new (my_end()) T(e);
                }
            }
        }
        ++size_;
    }
    void pop_back() {
        if (!small && s->counter > 1)
            realloc(s->capacity, my_begin(), my_end(), s);
        size_--;
        my_end()->~T();
    }

    bool empty() const noexcept {
        return size_ == 0;
    }

    size_t capacity() const noexcept {
        return (small ? SMALL_SIZE : s->capacity);
    }

    void reserve(size_t new_cap) {
        if (small && new_cap > SMALL_SIZE) {
            auto* new_st = new storage<T>;
            realloc(new_cap, my_begin(), my_end(), new_st);
            destruct_range(my_begin(), my_end());
            s = new_st;
            small = false;
        } else if (!small && (new_cap > s->capacity || (new_cap >= size_ && s->counter > 1))) {
            realloc(new_cap, my_begin(), my_end(), s);
        }
    }

    void shrink_to_fit() {
        if (!small && size_ != s->capacity) {
            realloc(std::max(size_, SMALL_SIZE), my_begin(), my_end(), s);
        }
    }

    void clear() noexcept {
        destruct_range(my_begin(), my_end());
        size_ = 0;
    }

    void swap(socow_vector& that) {
        if (small && that.small) {
            for (size_t i = 0; i < std::min(size_, that.size_); i++) {
                std::swap(static_storage[i], that.static_storage[i]);
            }
            try {
                if (size_ < that.size_) {
                    copy(that.static_storage.begin() + size_, that.static_storage.begin() + that.size_, static_storage.begin() + size_);
                    destruct_range(that.static_storage.begin() + size_, that.static_storage.begin() + that.size_);
                } else {
                    copy(static_storage.begin() + that.size_, static_storage.begin() + size_, that.static_storage.begin() + that.size_);
                    destruct_range(static_storage.begin() + that.size_, static_storage.begin() + size_);
                }
            } catch (...) {
                throw;
            }
        } else if (!small && !that.small) {
            std::swap(that.s, s);
        } else if (small && !that.small) {
            swap_small_big(*this, that);
        } else {
            swap_small_big(that, *this);
        }
        std::swap(that.size_, size_);
        std::swap(small, that.small);
    }
    iterator my_begin() {
        return (small ? static_storage.begin() : s->data_);
    }
    iterator my_end() {
        return my_begin() + size_;
    }
    iterator begin() {
        if (!small && s->counter > 1)
            realloc(s->capacity, my_begin(), my_end(), s);
        return (small ? static_storage.begin() : s->data_);
    }

    iterator end() {
        return begin() + size_;
    }

    const_iterator begin() const noexcept {
        return (small ? static_storage.begin() : s->data_);
    }

    const_iterator end() const noexcept {
        return begin() + size_;
    }

    iterator insert(const_iterator pos, T const& e) {
        size_t index = pos - my_begin();
        if (!small && s->counter > 1) {
            realloc(s->capacity, my_begin(), my_end(), s);
        }
        push_back(e);
        for (size_t i = size_ - 1; i > index; i--) {
            std::swap(*(my_begin() + i), *(my_begin() + i - 1));
        }
        return my_begin() + index;
    }

    iterator erase(const_iterator pos) {
        return erase(pos, pos + 1);
    }

    iterator erase(const_iterator first, const_iterator last) {
        size_t start = first - my_begin();
        size_t ending = last - my_begin();
        if (!small && s->counter > 1) {
            realloc(s->capacity, my_begin(), my_end(), s);
        }
        for (T* it = my_begin() + ending; it < my_end(); it++) {
            std::swap(*it, *(it - (ending - start)));
        }
        for (size_t i = 0; i < ending - start; i++) {
            pop_back();
        }
        return my_begin() + start;
    }

private:
    void copy(T* start, T* ending, T* destination) {
        for (T* it = start; it < ending; it++) {
            try {
                new(destination + (it - start)) T(*it);
            } catch (...) {
                destruct_range(destination, destination + (it - start));
                throw;
            }
        }
    }
    void swap_small_big(socow_vector& sm, socow_vector& big) {
        storage<T>* tmp = big.s;
        try {
            copy(sm.static_storage.begin(), sm.static_storage.begin() + sm.size_, big.static_storage.begin());
        } catch (...) {
            big.s = tmp;
            throw;
        }
        destruct_range(sm.my_begin(), sm.my_end());
        sm.s = tmp;
    }
    void realloc(size_t new_capacity, iterator start, iterator e, storage<T>*& st) {
        if (new_capacity == 0) {
            if (st->counter == 1) {
                st->~storage();
            }
            small = true;
            return;
        }
        size_ = e - start;
        T* new_data = static_cast<T*>(operator new(new_capacity * sizeof(T)));
        try {
            copy(start, e, new_data);
        } catch (...) {
            operator delete(new_data);
            throw;
        }
        if (st->data_ != nullptr && st->counter == 1) {
            destruct_range(st->data_, st->data_ + size_);
            operator delete(st->data_);
        } else if (st->data_ != nullptr){
            st->counter--;
            st = new storage<T>;
        }
        st->data_ = new_data;
        st->capacity = new_capacity;
    }
    static void destruct_range(T* start, T* end) noexcept {
        if (start == nullptr || end == nullptr) {
            return;
        }
        for (T* it = --end; it >= start; it--) {
            it->~T();
        }
    }
    union {
        std::array<T, SMALL_SIZE> static_storage;
        storage<T>* s;
    };
    size_t size_;
    bool small;
};
