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
            copy(that.static_storage.begin(), that.static_storage.end(), static_storage.begin());
        } else {
            new(&dynamic_storage) storage(that.dynamic_storage);
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
        if (small || dynamic_storage.unique()) {
            destruct_range(my_begin(), my_end());
        }
        if (!small) {
            dynamic_storage.~storage();
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
        return (small ? static_storage.begin() : dynamic_storage.get());
    }

    T const* data() const noexcept {
        return (small ? static_storage.begin() : dynamic_storage.get());
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
                T tmp = e;
                create_storage(SMALL_SIZE * 2);
                new(my_end()) T(tmp);
            } else {
                if (dynamic_storage.content_ptr->capacity_ == size_ || !dynamic_storage.unique()) {
                    T tmp = e;
                    new(&dynamic_storage) storage(realloc((dynamic_storage.content_ptr->capacity_ * (dynamic_storage.content_ptr->capacity_ == size_ ? 2 : 1)), my_begin(), my_end()));
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
        return (small ? SMALL_SIZE : dynamic_storage.content_ptr->capacity_);
    }

    void reserve(size_t new_cap) {
        if (small && new_cap > SMALL_SIZE) {
            create_storage(new_cap);
        } else if (!small && new_cap >= size_ && !dynamic_storage.unique()) {
            new(&dynamic_storage) storage(realloc(new_cap, my_begin(), my_end()));
        }
    }

    void shrink_to_fit() {
        if (!small) {
            if (size_ <= SMALL_SIZE) {
                big_to_small();
            } else if (size_ != dynamic_storage.content_ptr->capacity_) {
                new(&dynamic_storage) storage(realloc(size_, my_begin(), my_end()));
            }
        }
    }

    void clear() noexcept {
        if (!small && !dynamic_storage.unique()) {
            dynamic_storage = storage(capacity());
        } else {
            destruct_range(my_begin(), my_end());
        }
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
            std::swap(that.dynamic_storage.content_ptr,
                      dynamic_storage.content_ptr);
        } else if (small && !that.small) {
            swap_small_big(*this, that);
        } else {
            swap_small_big(that, *this);
        }
        std::swap(that.size_, size_);
        std::swap(small, that.small);
    }
    iterator my_begin() {
        return (small ? static_storage.begin() : dynamic_storage.get());
    }
    iterator my_end() {
        return my_begin() + size_;
    }
    iterator begin() {
        update_before_changes();
        return (small ? static_storage.begin() : dynamic_storage.get());
    }

    iterator end() {
        return begin() + size_;
    }

    const_iterator begin() const noexcept {
        return (small ? static_storage.begin() : dynamic_storage.get());
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
            new(&content_ptr->ref_counter) size_t(1);
            new(&content_ptr->capacity_) size_t(capacity);
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
    void create_storage(size_t new_cap) {
        storage new_st = realloc(new_cap, my_begin(), my_end());
        destruct_range(my_begin(), my_end());
        new(&dynamic_storage) storage(new_st);
        small = false;
    }
    void update_before_changes() {
        if (!small && !dynamic_storage.unique())
            new(&dynamic_storage) storage(realloc(
                dynamic_storage.content_ptr->capacity_, my_begin(), my_end()));
    }
    void big_to_small() {
        storage tmp = dynamic_storage;
        dynamic_storage.~storage();
        try {
            copy(tmp.get(), tmp.get() + size_, static_storage.begin());
        } catch (...) {
            new(&dynamic_storage) storage(tmp);
            throw;
        }
        destruct_range(tmp.get(), tmp.get() + size_);
        small = true;
    }

    void copy(T const* start, T const* ending, T* destination) {
        for (T const* it = start; it < ending; it++) {
            try {
                new(destination + (it - start)) T(*it);
            } catch (...) {
                destruct_range(destination, destination + (it - start));
                throw;
            }
        }
    }

    void swap_small_big(socow_vector& sm, socow_vector& big) {
        storage tmp = big.dynamic_storage;
        big.dynamic_storage.~storage();
        try {
            copy(sm.static_storage.begin(), sm.static_storage.begin() + sm.size_, big.static_storage.begin());
        } catch (...) {
            new(&big.dynamic_storage) storage(tmp);
            throw;
        }
        destruct_range(sm.my_begin(), sm.my_end());
        new(&sm.dynamic_storage) storage(tmp);
    }

    storage realloc(size_t new_capacity, const_iterator start, const_iterator e) {
        if (new_capacity == 0) {
            small = true;
            storage empty;
            return empty;
        }
        size_ = e - start;
        storage new_st(new_capacity);
        copy(start, e, new_st.get());
        if (!small) {
            if (dynamic_storage.get() != nullptr && dynamic_storage.unique()) {
                destruct_range(dynamic_storage.get(),
                               dynamic_storage.get() + size_);
            }
            dynamic_storage.~storage();
        }
        return new_st;
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
        storage dynamic_storage;
    };
};
