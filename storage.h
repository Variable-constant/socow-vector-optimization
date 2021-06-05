template <typename T>
struct storage {
    storage() : data_(nullptr), counter(1), capacity(0) {}
    ~storage() {
        operator delete(data_);
        counter = capacity = 0;
    }
    T* data_;
    size_t counter;
    size_t capacity;
};
