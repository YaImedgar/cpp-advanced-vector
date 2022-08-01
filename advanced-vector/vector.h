#pragma once
#include <cassert>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>

#define ISNOTHROWMOVEORCOPYCTOR std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>

template <typename T>
class RawMemory
{
public:
	RawMemory() = default;
	RawMemory(const RawMemory&) = delete;
	RawMemory& operator=(const RawMemory& rhs) = delete;

	RawMemory(RawMemory&& other) noexcept
	{
		buffer_ = std::move(other.buffer_);
		capacity_ = std::move(other.capacity_);
		other.buffer_ = nullptr;
		other.capacity_ = 0;
	}
	RawMemory& operator=(RawMemory&& rhs) noexcept
	{
		if (this != &rhs)
		{
			buffer_ = std::move(rhs.buffer_);
			capacity_ = std::move(rhs.capacity_);
			rhs.buffer_ = nullptr;
			rhs.capacity_ = 0;
		}
		return *this;
	}

	explicit RawMemory(size_t capacity)
		: buffer_(Allocate(capacity))
		, capacity_(capacity)
	{}

	~RawMemory()
	{
		Deallocate(buffer_);
	}

	T* operator+(size_t offset) noexcept
	{
		// Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
		assert(offset <= capacity_);
		return buffer_ + offset;
	}

	const T* operator+(size_t offset) const noexcept
	{
		return const_cast<RawMemory&>(*this) + offset;
	}

	const T& operator[](size_t index) const noexcept
	{
		return const_cast<RawMemory&>(*this)[index];
	}

	T& operator[](size_t index) noexcept
	{
		assert(index < capacity_);
		return buffer_[index];
	}

	void Swap(RawMemory& other) noexcept
	{
		std::swap(buffer_, other.buffer_);
		std::swap(capacity_, other.capacity_);
	}

	const T* GetAddress() const noexcept
	{
		return buffer_;
	}

	T* GetAddress() noexcept
	{
		return buffer_;
	}

	size_t Capacity() const
	{
		return capacity_;
	}

private:
	// Выделяет сырую память под n элементов и возвращает указатель на неё
	static T* Allocate(size_t n)
	{
		return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
	}

	// Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
	static void Deallocate(T* buf) noexcept
	{
		operator delete(buf);
	}

	T* buffer_ = nullptr;
	size_t capacity_ = 0;
};

template <typename T>
class Vector
{
public:
	using iterator = T*;
	using const_iterator = const T*;

	iterator begin() noexcept
	{
		return data_.GetAddress();
	}

	iterator end() noexcept
	{
		return data_.GetAddress() + size_;
	}

	const_iterator begin() const noexcept
	{
		return cbegin();
	}

	const_iterator end() const noexcept
	{
		return cend();
	}

	const_iterator cbegin() const noexcept
	{
		return const_cast<T*>(data_.GetAddress());
	}

	const_iterator cend() const noexcept
	{
		return const_cast<T*>(data_.GetAddress() + size_);
	}

	template <typename... Args>
	iterator Emplace(const_iterator pos, Args&&... args)
	{
		if (size_ == Capacity())
		{
			return EmplaceWithAllocation(pos, std::forward<Args>(args)...);
		}

		return EmplaceInPosition(pos, std::forward<Args>(args)...);
	}

	iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/
	{
		auto not_const_iter = const_cast<T*>(pos);
		std::destroy_at(not_const_iter);
		std::move(std::next(not_const_iter), end(), not_const_iter);
		--size_;
		return not_const_iter;
	}

	iterator Insert(const_iterator pos, const T& value)
	{
		return Emplace(pos, value);
	}

	iterator Insert(const_iterator pos, T&& value)
	{
		return Emplace(pos, std::move(value));
	}

	Vector() = default;
	Vector(Vector&& other) noexcept
	{
		data_ = std::move(other.data_);
		size_ = std::move(other.size_);
		other.size_ = 0;
	}

	Vector& operator=(const Vector& rhs)
	{
		if (this == &rhs)
		{
			return *this;
		}

		if (rhs.size_ > data_.Capacity())
		{
			Vector rhs_copy(rhs);
			Swap(rhs_copy);
			return *this;
		}

		if (rhs.size_ <= size_)
		{
			std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
			std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
			size_ = rhs.size_;
			return *this;
		}

		std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
		std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
		size_ = rhs.size_;

		return *this;
	}

	Vector& operator=(Vector&& rhs) noexcept
	{
		if (this != &rhs)
		{
			data_ = std::move(rhs.data_);
			size_ = std::move(rhs.size_);
			rhs.size_ = 0;
		}
		return *this;
	}

	explicit Vector(size_t size)
		: data_(size)
		, size_(size)
	{
		std::uninitialized_value_construct_n(data_.GetAddress(), size_);
	}

	Vector(const Vector& other)
		: data_(other.size_)
		, size_(other.size_)
	{
		std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
	}

	~Vector()
	{
		std::destroy_n(data_.GetAddress(), size_);
	}

	void Resize(size_t new_size)
	{
		if (new_size == size_)
		{
			return;
		}

		if (new_size < size_)
		{
			size_t num_to_destroy = size_ - new_size;
			std::destroy_n(data_.GetAddress() + new_size, num_to_destroy);
			size_ = new_size;
			return;
		}

		// new_size > size 
		Reserve(new_size);
		size_t num_to_construct = new_size - size_;
		std::uninitialized_default_construct_n(data_.GetAddress() + size_, num_to_construct);
		size_ = new_size;
	}

	void PushBack(const T& value)
	{
		if (size_ == Capacity())
		{
			RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
			std::uninitialized_copy_n(&value, 1, new_data.GetAddress() + size_);
			MoveDataTo(new_data);
		}
		else
		{
			std::uninitialized_copy_n(&value, 1, data_.GetAddress() + size_);
		}

		++size_;
	}

	void PushBack(T&& value)
	{
		if (size_ == Capacity())
		{
			RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
			std::uninitialized_move_n(&value, 1, new_data.GetAddress() + size_);
			MoveDataTo(new_data);
		}
		else
		{
			std::uninitialized_move_n(&value, 1, data_.GetAddress() + size_);
		}

		++size_;
	}

	void PopBack() noexcept
	{
		// https://en.cppreference.com/w/cpp/container/vector/pop_back
		// "Calling pop_back on an empty container results in undefined behavior."
		data_[--size_].~T();
	}

	void Swap(Vector& other) noexcept
	{
		data_.Swap(other.data_);
		std::swap(size_, other.size_);
	}

	void Reserve(size_t new_capacity)
	{
		if (new_capacity <= data_.Capacity())
		{
			return;
		}

		RawMemory<T> new_data(new_capacity);
		MoveDataTo(new_data);
	}

	size_t Size() const noexcept
	{
		return size_;
	}

	size_t Capacity() const noexcept
	{
		return data_.Capacity();
	}

	const T& operator[](size_t index) const noexcept
	{
		return const_cast<Vector&>(*this)[index];
	}

	T& operator[](size_t index) noexcept
	{
		assert(index < size_);
		return data_.GetAddress()[index];
	}

	template <typename... Args>
	T& EmplaceBack(Args&&... args)
	{
		return *Emplace(end(), std::forward<Args>(args)...);
	}

	T& Back()
	{
		return data_.GetAddress()[size_ - 1];
	}

private:
	void MoveDataTo(RawMemory<T>& new_data)
	{
		if constexpr (ISNOTHROWMOVEORCOPYCTOR)
		{
			std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
		}
		else
		{
			std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
		}
		DestroyAndSwap(new_data);
	}

	void DestroyAndSwap(RawMemory<T>& new_data)
	{
		std::destroy_n(data_.GetAddress(), size_);
		data_.Swap(new_data);
	}

	template <class InputIt, class NoThrowForwardIt>
	void MoveData(InputIt first, InputIt last, NoThrowForwardIt d_first)
	{
		if constexpr (ISNOTHROWMOVEORCOPYCTOR)
		{
			std::uninitialized_move(first, last, d_first);
		}
		else
		{
			std::uninitialized_copy(first, last, d_first);
		}
	}

	template <typename... Args>
	iterator EmplaceWithAllocation(const_iterator position, Args&&... args)
	{
		RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

		iterator pos = const_cast<T*>(position);
		size_t pos_num = std::distance(cbegin(), position);
		auto new_pos = new_data.GetAddress() + pos_num;

		new (new_pos) T(std::forward<Args>(args)...);

		try
		{
			MoveData(begin(), pos, new_data.GetAddress());
		}
		catch (...)
		{
			new_pos->~T();
			throw;
		}

		try
		{
			MoveData(pos, end(), new_data.GetAddress() + pos_num + 1);
		}
		catch (...)
		{
			std::destroy_n(new_data.GetAddress(), pos_num + 1);
			throw;
		}
		DestroyAndSwap(new_data);

		++size_;
		return new_pos;
	}

	template <typename... Args>
	iterator EmplaceInPosition(const_iterator position, Args&&... args)
	{
		iterator pos = const_cast<T*>(position);
		if (pos == end())
		{
			new (end()) T(std::forward<Args>(args)...);
		}
		else
		{
			T tmp_value = T(std::forward<Args>(args)...);
			new (data_.GetAddress() + size_) T(std::move(data_[size_ - 1]));

			std::move_backward(pos, std::prev(end()), end()); // can throw exception???
			*pos = std::move(tmp_value);
		}
		++size_;
		return pos;
	}

	RawMemory<T> data_;
	size_t size_ = 0;
};
