/***************************************************************
 *                                                             *
 *                Taiko Sanro - Arduino                        *
 *                 Cache data structure                        *
 *                                                             *
 *                        Chris                                *
 *                   wisaly@gmail.com                          *
 *                                                             *
 ***************************************************************/

#ifndef CACHE_H
#define CACHE_H

template <class T, int L>
class Cache {
public:
  Cache() { memset(data_, 0, sizeof(data_)); }
  inline void put(T value) {
    current_ = (current_ + 1) & (L - 1);
    data_[current_] = value;
  }
  inline T get(int offset = 0) const {
    return data_[(current_ + offset) & (L - 1)];
  }

private:
  T data_[L];
  int current_ = 0;
};

#endif  // CACHE_H