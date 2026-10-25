#include "dyeline/film.h"

namespace dyeline::film {

namespace {

#include "film_stocks.inc"

template <typename T, int N>
constexpr int count_of(const T (&)[N]) {
    return N;
}

int clamped(int index, int count) {
    return index < 0 ? 0 : (index >= count ? count - 1 : index);
}

}  // namespace

int negative_stock_count() {
    return count_of(kNegativeStocks);
}

const NegativeStock& negative_stock(int index) {
    return kNegativeStocks[clamped(index, negative_stock_count())];
}

int print_stock_count() {
    return count_of(kPrintStocks);
}

const PrintStock& print_stock(int index) {
    return kPrintStocks[clamped(index, print_stock_count())];
}

}  // namespace dyeline::film
