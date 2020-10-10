# ---------------------------------------------------------------------------
# MODERNDBS
# ---------------------------------------------------------------------------

add_executable(bm_expression bench/bm_expression.cc)
target_link_libraries(bm_expression moderndbs benchmark gtest gmock Threads::Threads)
