// include/incremental/incremental.hpp
// Umbrella include: include this single header to use the full incremental library.
//
//   #include <incremental/incremental.hpp>
//
// Consumers do not need to link against a compiled library.
// All core functionality is header-only.
#pragma once

// ── Core types ────────────────────────────────────────────────────────────────
#include <incremental/version.hpp>
#include <incremental/error.hpp>
#include <incremental/result.hpp>

// ── Graph model ───────────────────────────────────────────────────────────────
#include <incremental/node_handle.hpp>
#include <incremental/read_view.hpp>
#include <incremental/change_event.hpp>
#include <incremental/observer.hpp>
#include <incremental/graph.hpp>          // includes transaction.hpp internally

// ── Graph utilities ───────────────────────────────────────────────────────────
#include <incremental/cycle_detection.hpp>
#include <incremental/dependency_traversal.hpp>
#include <incremental/undo_redo.hpp>

// ── Generic API ───────────────────────────────────────────────────────────────
#include <incremental/customization_points.hpp>

// ── Terminal utilities (optional, opt-in) ─────────────────────────────────────
// Include separately if building a CLI:
//   #include <incremental/terminal.hpp>
