///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (C) 2019-2021, LAAS-CNRS, University of Edinburgh, University of Oxford
// Copyright note valid unless otherwise stated in individual files.
// All rights reserved.
///////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <typeinfo>
#include <boost/core/demangle.hpp>
#include "crocoddyl/core/utils/exception.hpp"

namespace crocoddyl {

template <typename Scalar>
IntegratedActionModelEulerTpl<Scalar>::IntegratedActionModelEulerTpl(
    boost::shared_ptr<DifferentialActionModelAbstract> model, const Scalar time_step, const bool with_cost_residual)
    : Base(model, time_step, with_cost_residual)
{
  init();
}

template <typename Scalar>
IntegratedActionModelEulerTpl<Scalar>::IntegratedActionModelEulerTpl(
    boost::shared_ptr<DifferentialActionModelAbstract> model, boost::shared_ptr<ControlAbstract> control, 
    const Scalar time_step, const bool with_cost_residual)
    : Base(model, control, time_step, with_cost_residual)
{
  init();
}

template <typename Scalar>
void IntegratedActionModelEulerTpl<Scalar>::init()
{
  time_step2_ = time_step_ * time_step_;
  enable_integration_ = true;
  VectorXs p_lb(nu_), p_ub(nu_);
  control_->convert_bounds(differential_->get_u_lb(), differential_->get_u_ub(), p_lb, p_ub);
  Base::set_u_lb(p_lb);
  Base::set_u_ub(p_ub);
  if (time_step_ < Scalar(0.)) {
    time_step_ = Scalar(1e-3);
    time_step2_ = time_step_ * time_step_;
    std::cerr << "Warning: dt should be positive, set to 1e-3" << std::endl;
  }
  if (time_step_ == Scalar(0.)) {
    enable_integration_ = false;
  }
}

template <typename Scalar>
IntegratedActionModelEulerTpl<Scalar>::~IntegratedActionModelEulerTpl() {}

template <typename Scalar>
void IntegratedActionModelEulerTpl<Scalar>::calc(const boost::shared_ptr<ActionDataAbstract>& data,
                                                 const Eigen::Ref<const VectorXs>& x,
                                                 const Eigen::Ref<const VectorXs>& u) {
  if (static_cast<std::size_t>(x.size()) != state_->get_nx()) {
    throw_pretty("Invalid argument: "
                 << "x has wrong dimension (it should be " + std::to_string(state_->get_nx()) + ")");
  }
  if (static_cast<std::size_t>(u.size()) != nu_) {
    throw_pretty("Invalid argument: "
                 << "u has wrong dimension (it should be " + std::to_string(nu_) + ")");
  }

  const std::size_t nv = differential_->get_state()->get_nv();

  // Static casting the data
  boost::shared_ptr<Data> d = boost::static_pointer_cast<Data>(data);

  // Computing the acceleration and cost
  control_->value(0.0, u, d->u_diff);
  differential_->calc(d->differential[0], x, d->u_diff);

  // Computing the next state (discrete time)
  const Eigen::VectorBlock<const Eigen::Ref<const VectorXs>, Eigen::Dynamic> v =
      x.tail(differential_->get_state()->get_nv());
  const VectorXs& a = d->differential[0]->xout;
  if (enable_integration_) {
    d->dx.head(nv).noalias() = v * time_step_ + a * time_step2_;
    d->dx.tail(nv).noalias() = a * time_step_;
    differential_->get_state()->integrate(x, d->dx, d->xnext);
    d->cost = time_step_ * d->differential[0]->cost;
  } else {
    d->dx.setZero();
    d->xnext = x;
    d->cost = d->differential[0]->cost;
  }

  // Updating the cost value
  if (with_cost_residual_) {
    d->r = d->differential[0]->r;
  }
}

template <typename Scalar>
void IntegratedActionModelEulerTpl<Scalar>::calcDiff(const boost::shared_ptr<ActionDataAbstract>& data,
                                                     const Eigen::Ref<const VectorXs>& x,
                                                     const Eigen::Ref<const VectorXs>& u) {
  if (static_cast<std::size_t>(x.size()) != state_->get_nx()) {
    throw_pretty("Invalid argument: "
                 << "x has wrong dimension (it should be " + std::to_string(state_->get_nx()) + ")");
  }
  if (static_cast<std::size_t>(u.size()) != nu_) {
    throw_pretty("Invalid argument: "
                 << "u has wrong dimension (it should be " + std::to_string(nu_) + ")");
  }

  const std::size_t nv = differential_->get_state()->get_nv();

  // Static casting the data
  boost::shared_ptr<Data> d = boost::static_pointer_cast<Data>(data);

  // Computing the derivatives for the time-continuous model (i.e. differential model)
  control_->value(0.0, u, d->u_diff);
  differential_->calcDiff(d->differential[0], x, d->u_diff);

  if (enable_integration_) {
    const MatrixXs& da_dx = d->differential[0]->Fx;
    const MatrixXs& da_du = d->differential[0]->Fu;
    d->Fx.topRows(nv).noalias() = da_dx * time_step2_;
    d->Fx.bottomRows(nv).noalias() = da_dx * time_step_;
    d->Fx.topRightCorner(nv, nv).diagonal().array() += Scalar(time_step_);

    control_->multiplyByDValue(0.0, u, da_du, d->da_du);
    d->Fu.topRows(nv).noalias() = time_step2_ * d->da_du;
    d->Fu.bottomRows(nv).noalias() = time_step_ * d->da_du;

    differential_->get_state()->JintegrateTransport(x, d->dx, d->Fx, second);
    differential_->get_state()->Jintegrate(x, d->dx, d->Fx, d->Fx, first, addto);
    differential_->get_state()->JintegrateTransport(x, d->dx, d->Fu, second);

    d->Lx.noalias() = time_step_ * d->differential[0]->Lx;
    // d->Lu.noalias() = time_step_ * d->differential[0]->Lu;
    control_->multiplyDValueTransposeBy(0.0, u, d->differential[0]->Lu, d->Lu);
    d->Lu *= time_step_;
    d->Lxx.noalias() = time_step_ * d->differential[0]->Lxx;
    // d->Lxu.noalias() = time_step_ * d->differential[0]->Lxu;
    control_->multiplyByDValue(0.0, u, d->differential[0]->Lxu, d->Lxu);
    d->Lxu *= time_step_;
    // d->Luu.noalias() = time_step_ * d->differential[0]->Luu;
    control_->multiplyByDValue(0.0, u, d->differential[0]->Luu, d->Ludiffu);
    control_->multiplyDValueTransposeBy(0.0, u, d->Ludiffu, d->Luu);
    d->Luu *= time_step_;
  } else {
    differential_->get_state()->Jintegrate(x, d->dx, d->Fx, d->Fx);
    d->Fu.setZero();
    d->Lx = d->differential[0]->Lx;
    d->Lu = d->differential[0]->Lu;
    d->Lxx = d->differential[0]->Lxx;
    d->Lxu = d->differential[0]->Lxu;
    d->Luu = d->differential[0]->Luu;
  }
}

template <typename Scalar>
boost::shared_ptr<ActionDataAbstractTpl<Scalar> > IntegratedActionModelEulerTpl<Scalar>::createData() {
  return boost::allocate_shared<Data>(Eigen::aligned_allocator<Data>(), this);
}

template <typename Scalar>
bool IntegratedActionModelEulerTpl<Scalar>::checkData(const boost::shared_ptr<ActionDataAbstract>& data) {
  boost::shared_ptr<Data> d = boost::dynamic_pointer_cast<Data>(data);
  if (data != NULL) {
    return differential_->checkData(d->differential[0]);
  } else {
    return false;
  }
}

template <typename Scalar>
void IntegratedActionModelEulerTpl<Scalar>::quasiStatic(const boost::shared_ptr<ActionDataAbstract>& data,
                                                        Eigen::Ref<VectorXs> u, const Eigen::Ref<const VectorXs>& x,
                                                        const std::size_t maxiter, const Scalar tol) {
  if (static_cast<std::size_t>(u.size()) != nu_) {
    throw_pretty("Invalid argument: "
                 << "u has wrong dimension (it should be " + std::to_string(nu_) + ")");
  }
  if (static_cast<std::size_t>(x.size()) != state_->get_nx()) {
    throw_pretty("Invalid argument: "
                 << "x has wrong dimension (it should be " + std::to_string(state_->get_nx()) + ")");
  }

  // Static casting the data
  boost::shared_ptr<Data> d = boost::static_pointer_cast<Data>(data);
  
  VectorXs uc(control_->get_nu());
  differential_->quasiStatic(d->differential[0], uc, x, maxiter, tol);
  control_->value_inv(0.0, uc, u);
}

template <typename Scalar>
void IntegratedActionModelEulerTpl<Scalar>::print(std::ostream& os) const {
  os << "IntegratedActionModelEuler {dt=" << time_step_ << ", " << *differential_ << "}";
}

}  // namespace crocoddyl
