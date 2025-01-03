#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>
#include <iostream>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept : buffer_(other.buffer_), capacity_(other.capacity_)
    {        
        other.capacity_ = 0;        
        other.buffer_ = nullptr;
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept
    {
        if (this != &rhs)
        {
            Swap(rhs);            
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept : data_(std::move(other.data_)), size_(other.size_) 
    {
        other.size_ = 0;
    }

    Vector& operator=(const Vector& rhs)
    {
        if (this == &rhs)
        {
            return *this;
        }
        if (rhs.size_ > Capacity())
        {           
            Vector to_copy(rhs);
            Swap(to_copy);            
        }
        else
        {
            OpAssignRhsSizeLessCap(rhs);            
        }
        size_ = rhs.size_;
        return *this;            
    }

    Vector& operator=(Vector&& rhs) noexcept
    {
        if (this != &rhs) {            
            Swap(rhs);            
        }
        return *this;
    }

    void Resize(size_t new_size)
    {
        if (size_ == new_size)
        {
            return;
        }
        if (new_size < size_)
        {
            std::destroy_n(data_ + new_size, size_ - new_size);
        }
        else
        {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value)
    {  
        PushBackU([&](T* dest)
            {
                std::uninitialized_copy_n(&value, 1, dest + size_);
                return nullptr;
            });        
    }
    void PushBack(T&& value)
    {  
        PushBackU([&](T* dest)
            {
                std::uninitialized_move_n(&value, 1, dest + size_);
                return nullptr;
            });               
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args)
    {        
        return *PushBackU([&](T* dest)
            {
                return new (dest + size_) T(std::forward<Args>(args)...);
            });                
    }

    void PopBack()
    {
        std::destroy_n(data_ + size_ - 1, 1);
        size_--;
    }
    void Swap(Vector& other) noexcept
    {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);           
    }

    void Reserve(size_t new_capacity) {         
        if (new_capacity > Capacity())
        {
            RawMemory<T> new_data(new_capacity);
            CopySwap(new_data);
        }
    }

    size_t Size() const noexcept {     
        return size_;
    }

    size_t Capacity() const noexcept {        
        return data_.Capacity();
    }

    iterator begin() noexcept
    {
        return begin_();
    }
    iterator end() noexcept
    {
        return end_();
    }
    const_iterator begin() const noexcept
    {
        return begin_();
    }

    const_iterator end() const noexcept        
    {
        return end_();
    }
    const_iterator cbegin() const noexcept
    {
        return begin_();
    }
    const_iterator cend() const noexcept
    {
        return end_();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args)
    {
        assert(pos >= begin() && pos <= end());
        iterator res;        
        iterator pos_nc = const_cast<T*>(pos);
        
        if (size_ < Capacity() && size_ != 0)
        {
            T* tmp = new T(std::forward<Args>(args)...);
            CopyOrMove(data_ + size_ - 1, 1, data_ + size_);
            std::move_backward(pos_nc, data_ + size_ - 1, data_ + size_);               
            *pos_nc = std::move(*tmp);
            std::destroy_n(tmp, 1);
            res = pos_nc;
        } 
        else
        {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            auto dist = std::distance(data_.GetAddress(), pos_nc);        
            res = new (new_data + dist) T(std::forward<Args>(args)...);         
            if (size_ != 0)
            {
                CopyOrMove(data_.GetAddress(), (size_t)dist, new_data.GetAddress());
                CopyOrMove(pos_nc, size_ - (size_t)dist, new_data + dist + 1);                
                std::destroy_n(data_.GetAddress(), size_);
            }           
            new_data.Swap(data_);            
        }      
        size_++;
        return res;
    }

    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/
    {  
        assert(pos >= begin() && pos < end());
        T* pos_nc = const_cast<T*>(pos);
        std::move(pos_nc + 1, data_ + size_, pos_nc);
        std::destroy_n(data_ + size_ - 1, 1);  
        size_--;
        return pos_nc;
    }
    iterator Insert(const_iterator pos, const T& value)
    {
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value)
    {
        return Emplace(pos, std::move(value));
    }
    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    template<typename F>
    T* PushBackU(F copy_move)
    {
        T* res;
        if (size_ != Capacity()) {           
            res = copy_move(data_.GetAddress());
        }
        else
        {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);          
            res = copy_move(new_data.GetAddress());
            CopySwap(new_data);
        }
        size_++;
        return res;
    }

    void CopyOrMove(T* from, size_t num, T* to)
    {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(from, num, to);
        }
        else {
            std::uninitialized_copy_n(from, num, to);
        }
    }

    void CopySwap(RawMemory<T>& new_data)
    {
        CopyOrMove(data_.GetAddress(), size_, new_data.GetAddress());        
        std::destroy_n(data_.GetAddress(), size_);
        new_data.Swap(data_);
    } 
    
    iterator begin_() const
    {
        return const_cast<T*>(data_.GetAddress());
    }

    iterator end_() const
    {
        return const_cast<T*>(data_ + size_);
    }

    void OpAssignRhsSizeLessCap(const Vector& rhs)
    {
        std::copy(rhs.data_.GetAddress(), rhs.data_ + std::min(rhs.size_, size_), data_.GetAddress());
        if (rhs.size_ < size_)
        {
            std::destroy_n(data_ + rhs.size_, size_ - rhs.size_);
        }
        if (rhs.size_ > size_)
        {
            std::uninitialized_copy_n(rhs.data_ + size_, rhs.size_ - size_, data_ + size_);
        }
    }

    RawMemory<T> data_;   
    size_t size_ = 0;
};
