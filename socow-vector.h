#pragma once
#include <cstddef>
#include <array>

template <typename T, size_t SMALL_SIZE>
struct socow_vector {
    using iterator = T*;
    using const_iterator = T const*;

    socow_vector() noexcept
        : size_(0), small(true) {}

    socow_vector(socow_vector const& that) : socow_vector() {
        if (that.small) {
            copy(const_cast<T*>(that.static_storage.begin()), const_cast<T*>(that.static_storage.end()), static_storage.begin());
        } else {
            new(&s) storage(that.s);
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
        if (small || s.unique()) {
            destruct_range(my_begin(), my_end());
        }
        if (!small) {
            s.~storage();
        }
        size_ = 0;
    }

    T& operator[](size_t i) {
        update_before_changes();
        return *(my_begin() + i);
    }

    T const& operator[](size_t i) const noexcept {
        return *(begin() + i);
    }

    T* data() {
        update_before_changes();
        return (small ? static_storage.begin() : s.get());
    }

    T const* data() const noexcept {
        return (small ? static_storage.begin() : s.get());
    }

    size_t size() const noexcept {
        return size_;
    }

    T& front() {
        update_before_changes();
        return *my_begin();
    }

    T const& front() const noexcept {
        return *begin();
    }

    T& back() {
        update_before_changes();
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
                storage new_st;
                realloc(SMALL_SIZE * 2, my_begin(), my_end(), new_st);
                destruct_range(my_begin(), my_end());
                T tmp = e;
                new(&s) storage(new_st);
                small = false;
                new(my_end()) T(tmp);
            } else {
                if (s.content_ptr->capacity_ == size_ || !s.unique()) {
                    T tmp = e;
                    realloc((s.content_ptr->capacity_ * (s.content_ptr->capacity_ == size_ ? 2 : 1)), my_begin(), my_end(), s);
                    new (my_end()) T(tmp);
                } else {
                    new (my_end()) T(e);
                }
            }
        }
        ++size_;
    }
    void pop_back() {
        update_before_changes();
        size_--;
        my_end()->~T();
    }

    bool empty() const noexcept {
        return size_ == 0;
    }

    size_t capacity() const noexcept {
        return (small ? SMALL_SIZE : s.content_ptr->capacity_);
    }

    void reserve(size_t new_cap) {
        if (small && new_cap > SMALL_SIZE) {
            storage new_st;
            realloc(new_cap, my_begin(), my_end(), new_st);
            destruct_range(my_begin(), my_end());
            new(&s) storage(new_st);
            small = false;
        } else if (!small && (new_cap > s.content_ptr->capacity_ || (new_cap >= size_ && !s.unique()))) {
            realloc(new_cap, my_begin(), my_end(), s);
        }
    }

    void shrink_to_fit() {
        if (!small) {
            if (size_ <= SMALL_SIZE) {
                big_to_small();
            } else if (size_ != s.content_ptr->capacity_) {
                realloc(size_, my_begin(), my_end(), s);
            }
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
            if (size_ < that.size_) {
                copy(that.static_storage.begin() + size_, that.static_storage.begin() + that.size_, static_storage.begin() + size_);
                destruct_range(that.static_storage.begin() + size_, that.static_storage.begin() + that.size_);
            } else {
                copy(static_storage.begin() + that.size_, static_storage.begin() + size_, that.static_storage.begin() + that.size_);
                destruct_range(static_storage.begin() + that.size_, static_storage.begin() + size_);
            }
        } else if (!small && !that.small) {
            std::swap(that.s.content_ptr, s.content_ptr);
        } else if (small && !that.small) {
            swap_small_big(*this, that);
        } else {
            swap_small_big(that, *this);
        }
        std::swap(that.size_, size_);
        std::swap(small, that.small);
    }
    iterator my_begin() {
        return (small ? static_storage.begin() : s.get());
    }
    iterator my_end() {
        return my_begin() + size_;
    }
    iterator begin() {
        update_before_changes();
        return (small ? static_storage.begin() : s.get());
    }

    iterator end() {
        return begin() + size_;
    }

    const_iterator begin() const noexcept {
        return (small ? static_storage.begin() : s.get());
    }

    const_iterator end() const noexcept {
        return begin() + size_;
    }

    iterator insert(const_iterator pos, T const& e) {
        size_t index = pos - my_begin();
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
        update_before_changes();
        for (T* it = my_begin() + ending; it < my_end(); it++) {
            std::swap(*it, *(it - (ending - start)));
        }
        for (size_t i = 0; i < ending - start; i++) {
            pop_back();
        }
        return my_begin() + start;
    }

private:
    struct content {
        size_t ref_counter;
        size_t capacity_;
        T data_[];
    };

    struct storage {
        content* content_ptr;

        storage() : content_ptr(nullptr){}

        explicit storage(size_t capacity) : content_ptr(static_cast<content*>(operator new(sizeof(content) + sizeof(T) * capacity, static_cast<std::align_val_t>(alignof(content))))) {
            content_ptr->ref_counter = 1;
            content_ptr->capacity_ = capacity;
        }

        storage(storage const& other) : content_ptr(other.content_ptr) {
            content_ptr->ref_counter++;
        }

        storage& operator=(storage const& other) {
            if (&other != this) {
                storage tmp(other);
                std::swap(tmp.content_ptr, this->content_ptr);
            }
            return *this;
        }

        ~storage() {
            if (content_ptr->ref_counter == 1) {
                operator delete(content_ptr, static_cast<std::align_val_t>(alignof(content)));
            } else {
                content_ptr->ref_counter--;
            }
        }

        T* get() {
            return content_ptr->data_;
        }
        T* get() const {
            return content_ptr->data_;
        }
        bool unique() {
            return content_ptr->ref_counter == 1;
        }
    };
    void update_before_changes() {
        if (!small && !s.unique())
            realloc(s.content_ptr->capacity_, my_begin(), my_end(), s);
    }
    void big_to_small() {
        storage tmp = s;
        s.~storage();
        try {
            copy(tmp.get(), tmp.get() + size_, static_storage.begin());
        } catch (...) {
            new(&s) storage(tmp);
            throw;
        }
        destruct_range(tmp.get(), tmp.get() + size_);
        small = true;
    }

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
        storage tmp = big.s;
        big.s.~storage();
        try {
            copy(sm.static_storage.begin(), sm.static_storage.begin() + sm.size_, big.static_storage.begin());
        } catch (...) {
            new(&big.s) storage(tmp);
            throw;
        }
        destruct_range(sm.my_begin(), sm.my_end());
        new(&sm.s) storage(tmp);
    }

    void realloc(size_t new_capacity, iterator start, iterator e, storage& st) {
        if (new_capacity == 0) {
            small = true;
            return;
        }
        size_ = e - start;
        storage new_st(new_capacity);
        copy(start, e, new_st.get());
        if (!small) {
            if (st.get() != nullptr && st.unique()) {
                destruct_range(st.get(), st.get() + size_);
            }
            st.~storage();
        }
        new(&st) storage(new_st);
    }
    static void destruct_range(T* start, T* end) noexcept {
        if (start == nullptr || end == nullptr) {
            return;
        }
        for (T* it = --end; it >= start; it--) {
            it->~T();
        }
    }

private:
    size_t size_;
    bool small;
    union {
        std::array<T, SMALL_SIZE> static_storage;
        storage s;
    };
};
