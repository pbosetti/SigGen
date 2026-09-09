// SigGen -- composition of several signals into one.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// Summing covers the common case of a carrier plus harmonics, drift and
// noise; multiplying covers amplitude modulation, where an envelope shapes a
// carrier. Every component is stepped on every sample in both modes, so the
// components stay in lockstep and a mode change does not shift their phases.

#pragma once

#include "signal.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace SigGen {

/// Several signals combined by a single operator.
///
/// A composite owns clones of its components, so a signal can be added and
/// then modified or destroyed without affecting the composition. It carries no
/// intrinsic noise of its own: its components already do, and stacking one
/// more layer per level of nesting would make the noise floor depend on how
/// the signal happened to be structured.
class Composite : public CloneableSignal<Composite> {
public:
  /// How the component samples are combined.
  enum class Op {
    SUM,      ///< Add the components -- carrier plus harmonics plus noise.
    MULTIPLY, ///< Multiply them -- a carrier shaped by an envelope.
  };

  explicit Composite(Op op = Op::SUM)
      : CloneableSignal(std::nullopt), _op(op) {}

  Composite(const Composite &other) : CloneableSignal(other), _op(other._op) {
    _components.reserve(other._components.size());
    for (const Component &c : other._components)
      _components.push_back({c.signal->clone(), c.gain});
  }

  Composite &operator=(const Composite &other) {
    if (this != &other) {
      Composite copy(other);
      *this = std::move(copy);
    }
    return *this;
  }

  Composite(Composite &&) = default;
  Composite &operator=(Composite &&) = default;
  ~Composite() override = default;

  Op op() const { return _op; }
  void set_op(Op op) { _op = op; }

  /// Add a copy of a signal, scaled by `gain`. The component inherits the
  /// composite's sampling rate.
  void add(const Signal &signal, double gain = 1.0) {
    add(signal.clone(), gain);
  }

  /// Add a signal the composite takes ownership of, scaled by `gain`.
  void add(std::unique_ptr<Signal> signal, double gain = 1.0) {
    if (!signal)
      throw SigGenException("cannot add a null component to a composite");
    signal->set_sample_rate(sample_rate());
    signal->set_seed(derived_seed(_components.size()));
    _components.push_back({std::move(signal), gain});
  }

  std::size_t size() const { return _components.size(); }
  bool empty() const { return _components.empty(); }

  const Signal &component(std::size_t index) const { return *at(index).signal; }
  Signal &component(std::size_t index) { return *at(index).signal; }

  /// Scale factor applied to a component before combining.
  double gain(std::size_t index) const { return at(index).gain; }
  void set_gain(std::size_t index, double gain) { at(index).gain = gain; }

  std::string type() const override { return "composite"; }

  void set_sample_rate(double fs) override {
    Signal::set_sample_rate(fs);
    for (Component &c : _components)
      c.signal->set_sample_rate(fs);
  }

  /// Reseed the composite and give every component its own derived seed, so
  /// that two identical components do not emit identical noise.
  void set_seed(std::uint64_t seed) override {
    Signal::set_seed(seed);
    for (std::size_t i = 0; i < _components.size(); ++i)
      _components[i].signal->set_seed(derived_seed(i));
  }

  void reset() override {
    Signal::reset();
    for (Component &c : _components)
      c.signal->reset();
  }

  /// RMS of the combination, assuming the components are mutually
  /// uncorrelated: powers add under SUM, amplitudes multiply under MULTIPLY.
  /// The assumption holds for the usual mix of noise and harmonically
  /// unrelated tones, and is an approximation otherwise.
  double rms() const override {
    if (_components.empty())
      return 0.0;
    if (_op == Op::SUM) {
      double power = 0.0;
      for (const Component &c : _components) {
        const double contribution = c.gain * c.signal->rms();
        power += contribution * contribution;
      }
      return std::sqrt(power);
    }
    double product = 1.0;
    for (const Component &c : _components)
      product *= std::abs(c.gain) * c.signal->rms();
    return product;
  }

protected:
  double sample() override {
    if (_components.empty())
      return 0.0;
    if (_op == Op::SUM) {
      double total = 0.0;
      for (Component &c : _components)
        total += c.gain * c.signal->next();
      return total;
    }
    double product = 1.0;
    for (Component &c : _components)
      product *= c.gain * c.signal->next();
    return product;
  }

private:
  struct Component {
    std::unique_ptr<Signal> signal;
    double gain = 1.0;
  };

  const Component &at(std::size_t index) const {
    if (index >= _components.size())
      throw SigGenException("component index " + std::to_string(index) +
                            " is out of range");
    return _components[index];
  }
  Component &at(std::size_t index) {
    return const_cast<Component &>(
        static_cast<const Composite &>(*this).at(index));
  }

  /// Distinct but reproducible seed for the i-th component.
  std::uint64_t derived_seed(std::size_t index) const {
    return seed() + 0x9E3779B97F4A7C15ULL * (index + 1);
  }

  Op _op;
  std::vector<Component> _components;
}; // class Composite

} // namespace SigGen
