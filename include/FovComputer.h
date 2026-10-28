#pragma once

#include <cmath>

/**
 * Compute fov(field of view) using recursive shadowcasting algorithm.
 * Optimized version based on performance analysis.
 */
namespace krishna {
namespace FovComputer {

/**
 * Compute fov(field of view) using recursive shadowcasting algorithm.
 *
 * This is the map coordinate system:
 *    +----->-------------------------map--+ -
 *    |                                    | |
 *    |                                    | |
 *    V           @                        | mapHeight(y direction to bottom)
 *    |                                    | |
 *    |                                    | |
 *    +------------------------------------+ -
 *    |----mapWidth(x direction to right)--|
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * @param x             The hero's x position on the map.
 * @param y             The hero's y position on the map.
 * 
 * @param isBlocking    The caller should provide a function like:
 *                      bool isBlocking(int x, int y)const
 *                      to check whether a tile at(x,y) block sight.

 * @param markVisible   The caller should provide a function like:
 *                      void markVisible(int x, int y)
 *                      to denote that a tile at(x,y) is visible and the caller can mark it.
 *
 * @param radius        The hero's max line-of-sight or max scanning depth.
 */
template <class IsBlocking, class MarkVisible>
void ComputeFov(int x, int y, IsBlocking&& isBlocking, MarkVisible&& markVisible, int radius = 128);

//
//
//
//*************************************************************************
//                                                                        *
//                                                                        *
//                                                                        *
//                    Implementation Algorithm Section                    *
//                                                                        *
//                                                                        *
//                                                                        *
//*************************************************************************

#ifdef DEBUG
#include <cassert>
#ifndef AssertTrue
#define AssertTrue(x) assert(x)
#endif
#else
#ifndef AssertTrue
#define AssertTrue(x)
#endif
#endif

// Compiler optimization hints
#ifdef __GNUC__
#define HOT __attribute__((hot, always_inline))
#define UNREACHABLE __builtin_unreachable()
#elif defined(__clang__)
#define HOT __attribute__((hot, always_inline))
#define UNREACHABLE __builtin_unreachable()
#elif defined(_MSC_VER)
#define HOT __forceinline
#define UNREACHABLE __assume(0)
#else
#define HOT inline
#define UNREACHABLE
#endif

/**
 * Vector2d is used to present points in 2d coordinate system.
 */
class Vector2d {
 public:
  int x;
  int y;

  Vector2d() = default;
  constexpr Vector2d(int x, int y) : x(x), y(y) {}
  Vector2d(const Vector2d& other) = default;
  Vector2d& operator=(const Vector2d& other) = default;

  Vector2d& operator+=(const Vector2d& other) {
    this->x += other.x;
    this->y += other.y;
    return *this;
  }

  Vector2d& operator-=(const Vector2d& other) {
    this->x -= other.x;
    this->y -= other.y;
    return *this;
  }

  void clear(){
    this->x = 0;
    this->y = 0;
  }

  bool operator<(const Vector2d& other) const {
    if (this->x != other.x) {
      return this->x < other.x;
    }
    return this->y < other.y;
  }

  bool operator==(const Vector2d& other) const {
    return (this->x == other.x) && (this->y == other.y);
  }
};  // end class Vector2d

/**
 * TileArray is used to cache a visible tile array.
 * Optimized: Marked as final, methods inlined.
 */
class TileArray final {
 public:
  TileArray(int depth, int xi, int xj) : depth_(depth), it_(xi), xj_(xj) {}

  bool hasNextTile() const { return it_ <= xj_; }

  Vector2d getNextTile() {
    AssertTrue(hasNextTile());
    return Vector2d{it_++, depth_};
  }

 private:
  int depth_;
  int it_;
  int xj_;
};

/**
 * Quadrant is used to transform coordinate system.
 * Optimized: Removed static Vector2d (thread-unsafe, optimization barrier).
 *            Replaced function pointer array with switch-case for better inlining.
 *            Marked transform methods as const, return by value (RVO eliminates copy).
 */
class Quadrant final {
 public:
  Quadrant(int direction, const Vector2d& origin): origin_(origin), direction_(direction) {}

  // Optimized: switch-case instead of member function pointer array.
  // Returns by value; compiler applies RVO/NRVO, no actual copy.
  HOT Vector2d transform(int x, int y) const {
    AssertTrue(0 <= direction_ && direction_ <= 3);
    switch (direction_) {
      case 0:
        return Vector2d(origin_.x + x, origin_.y + y);   // South
      case 1:
        return Vector2d(origin_.x + y, origin_.y - x);   // East
      case 2:
        return Vector2d(origin_.x - x, origin_.y - y);   // North
      case 3:
        return Vector2d(origin_.x - y, origin_.y + x);   // West
    }
    UNREACHABLE;
    return Vector2d(origin_.x, origin_.y);  // Never reached
  }

 private:
  Vector2d origin_;
  int direction_;
};

/**
 * FovRegion represents a visible region.
 * Optimized: Kept as POD-like struct to ensure cache-friendly layout.
 *            Added explicit inline hints for accessors.
 */
class FovRegion final {
 public:
  FovRegion(int depth, float startSlope, float endSlope): depth_(depth), startSlope_(startSlope), endSlope_(endSlope) {}

  HOT TileArray getTileArray() const {
    int minCol = roundTilesUp(static_cast<float>(depth_) * this->startSlope_);
    int maxCol = roundTilesDown(static_cast<float>(depth_) * this->endSlope_);
    return TileArray(depth_, minCol, maxCol);
  }

  HOT FovRegion getNextFovRegion() const {
    return FovRegion(depth_ + 1, startSlope_, endSlope_);
  }

  float getStartSlope() const { return startSlope_; }

  void setStartSlope(float value) { startSlope_ = value; }

  float getEndSlope() const { return endSlope_; }

  void setEndSlope(float value) { endSlope_ = value; }

  int getDepth() const { return depth_; }

 private:
  int depth_;
  float startSlope_;
  float endSlope_;

  static inline int roundTilesUp(float n) {
    return static_cast<int>(floorf(n + 0.5f));
  }

  static inline int roundTilesDown(float n) {
    return static_cast<int>(ceilf(n - 0.5f));
  }
};

/**
 * Compute slope for a tile.
 */
inline float slope(const Vector2d& tile) {
  AssertTrue(1 <= tile.y);
  return (static_cast<float>(tile.x) - 0.5f) / static_cast<float>(tile.y);
}

/**
 * Check if a tile is symmetric within the FOV region.
 */
inline bool isSymmetric(const FovRegion& fovRegion, const Vector2d& tile) {
  float depthF = static_cast<float>(fovRegion.getDepth());
  return (depthF * fovRegion.getStartSlope() <= static_cast<float>(tile.x) && 
          static_cast<float>(tile.x) <= depthF * fovRegion.getEndSlope());
}

/**
 * Reveal a tile (mark as visible).
 * Optimized: Marked HOT, pass quadrant by const reference.
 */
template <class MarkVisible>
HOT void reveal(const Vector2d& tile, const Quadrant& quadrant, MarkVisible& markVisible) {
  Vector2d p = quadrant.transform(tile.x, tile.y);
  markVisible(p.x, p.y);
}

/**
 * Check if a tile is a wall (blocking).
 * Optimized: Removed hasInit() check (handled by loop restructuring).
 *            Marked HOT, pass quadrant by const reference.
 */
template <class IsBlocking>
HOT bool isWall(const Vector2d& tile, const Quadrant& quadrant, IsBlocking& isBlocking) {
  Vector2d p = quadrant.transform(tile.x, tile.y);
  return isBlocking(p.x, p.y);
}

/**
 * Check if a tile is floor (non-blocking).
 * Optimized: Removed hasInit() check.
 *            Marked HOT, pass quadrant by const reference.
 */
template <class IsBlocking>
HOT bool isFloor(const Vector2d& tile, const Quadrant& quadrant, IsBlocking& isBlocking) {
  Vector2d p = quadrant.transform(tile.x, tile.y);
  return !isBlocking(p.x, p.y);
}

/**
 * Scan a region recursively.
 * Optimized:
 *   - Marked HOT to force inlining of the entire scan loop.
 *   - Pass fovRegion by const reference where possible, by value for recursion.
 *   - Restructured loop to avoid hasInit() checks on prevTile.
 *   - Pass quadrant by const reference.
 */
template <class IsBlocking, class MarkVisible>
HOT void scan(const FovRegion& fovRegion, const Quadrant& quadrant, IsBlocking& isBlocking, MarkVisible& markVisible, int radius) {
  if (fovRegion.getDepth() > radius) {
    return;
  }

  auto tileArray = fovRegion.getTileArray();

  // Handle empty tile arrays gracefully
  if (!tileArray.hasNextTile()) {
    return;
  }

  // Process first tile separately - no prevTile dependency
  Vector2d prevTile = tileArray.getNextTile();
  if (isWall(prevTile, quadrant, isBlocking) || isSymmetric(fovRegion, prevTile)) {
    reveal(prevTile, quadrant, markVisible);
  }

  // Process remaining tiles with prevTile always valid
  while (tileArray.hasNextTile()) {
    Vector2d tile = tileArray.getNextTile();

    if (isWall(tile, quadrant, isBlocking) || isSymmetric(fovRegion, tile)) {
      reveal(tile, quadrant, markVisible);
    }

    // Wall -> Floor transition: adjust start slope
    if (isWall(prevTile, quadrant, isBlocking) && isFloor(tile, quadrant, isBlocking)) {
      const_cast<FovRegion&>(fovRegion).setStartSlope(slope(tile));
    }

    // Floor -> Wall transition: spawn new scan region
    if (isFloor(prevTile, quadrant, isBlocking) && isWall(tile, quadrant, isBlocking)) {
      FovRegion nextFovRegion = fovRegion.getNextFovRegion();
      nextFovRegion.setEndSlope(slope(tile));
      scan(nextFovRegion, quadrant, isBlocking, markVisible, radius);
    }

    prevTile = tile;
  }

  // If last tile was floor, continue scanning next depth
  if (isFloor(prevTile, quadrant, isBlocking)) {
    scan(fovRegion.getNextFovRegion(), quadrant, isBlocking, markVisible, radius);
  }
}

/**
 * Main FOV computation entry point.
 */
template <class IsBlocking, class MarkVisible>
void ComputeFov(int x, int y, IsBlocking&& isBlocking, MarkVisible&& markVisible, int radius) {
  markVisible(x, y);
  Vector2d origin(x, y);
  for (int i = 0; i < 4; i++) {
    Quadrant quadrant(i, origin);
    FovRegion fovRegion(1, -1.0f, 1.0f);
    // Pass isBlocking and markVisible as lvalue references to ensure inlining
    scan(fovRegion, quadrant, isBlocking, markVisible, radius);
  }
} // end ComputeFov

}  // end namespace FovComputer

}  // end namespace krishna