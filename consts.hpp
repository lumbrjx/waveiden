constexpr double PI = 3.14159265358979323846;

struct Complex {
  double re, im;
  Complex(double r = 0, double i = 0) : re(r), im(i) {}
  Complex operator+(const Complex &o) const { return {re + o.re, im + o.im}; }
  Complex operator-(const Complex &o) const { return {re - o.re, im - o.im}; }
  Complex operator*(const Complex &o) const {
    return {re * o.re - im * o.im, re * o.im + im * o.re};
  }
};
