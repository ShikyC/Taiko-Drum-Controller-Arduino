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
  Cache ();
  void put (T value);
  T get (int offset = 0) const;

private:
  T data_ [L];
  int current_ = 0;
};

template <class T, int L>
Cache <T, L>::Cache () {
  for (int i = 0; i < L; i++) {
    data_ [i] = 0;
  }
}

template <class T, int L>
void Cache <T, L>::put (T value) {
  data_ [current_] = value;
  current_ = (current_ + 1) % L;
}

template <class T, int L>
T Cache <T, L>::get (int offset) const {
   int index = (current_ + offset) % L;
   return data_ [index];
}

#endif // CACHE_H
