#pragma once

#include <memory>

template <typename T>
class Box : private std::unique_ptr<T> {
 public:
  // NOLINTNEXTLINE
  Box(T&& value) : std::unique_ptr<T>(std::make_unique<T>(std::move(value))) {}
  Box& operator=(T&& value) {
    std::unique_ptr<T>::operator=(std::make_unique<T>(std::move(value)));
    return *this;
  }

  Box(const Box& other) : std::unique_ptr<T>(std::make_unique<T>(*other)) {}
  Box& operator=(const Box& other) {
    if (this != &other) {
      std::unique_ptr<T>::operator=(std::make_unique<T>(*other));
    }
    return *this;
  }

  Box(Box&&) noexcept = default;
  Box& operator=(Box&&) noexcept = default;

  ~Box() = default;

  using std::unique_ptr<T>::get;
  using std::unique_ptr<T>::operator*;
  using std::unique_ptr<T>::operator->;

  friend bool operator==(const Box& left, const Box& right) {
    return *left == *right;
  }
};
