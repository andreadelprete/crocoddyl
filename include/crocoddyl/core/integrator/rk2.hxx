///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (C) 2021, LAAS-CNRS, IRI: CSIC-UPC, University of Edinburgh,
//                     University of Trento
// Copyright note valid unless otherwise stated in individual files.
// All rights reserved.
///////////////////////////////////////////////////////////////////////////////

#include <iostream>

#include "crocoddyl/core/utils/exception.hpp"
#include "crocoddyl/core/integrator/rk2.hpp"

using namespace std;

namespace crocoddyl {

template <typename Scalar>
IntegratedActionModelRK2Tpl<Scalar>::IntegratedActionModelRK2Tpl(
    boost::shared_ptr<DifferentialActionModelAbstract> model, const Scalar time_step, const bool with_cost_residual)
    : Base(model, time_step, with_cost_residual)
{
  init();
}

template <typename Scalar>
IntegratedActionModelRK2Tpl<Scalar>::IntegratedActionModelRK2Tpl(
  boost::shared_ptr<DifferentialActionModelAbstract> model, boost::shared_ptr<ControlAbstract> control,
  const Scalar time_step, const bool with_cost_residual)
  : Base(model, control, time_step, with_cost_residual)
{
  init();
}

template <typename Scalar>
void IntegratedActionModelRK2Tpl<Scalar>::init()
{
  VectorXs p_lb(nu_), p_ub(nu_);
  control_->convert_bounds(differential_->get_u_lb(), differential_->get_u_ub(), p_lb, p_ub);
  Base::set_u_lb(p_lb);
  Base::set_u_ub(p_ub);
  if (time_step_ < Scalar(0.)) {
    time_step_ = Scalar(1e-3);
    std::cerr << "Warning: dt should be positive, set to 1e-3" << std::endl;
  }
  enable_integration_ = true; 
  if (time_step_ == Scalar(0.)) {
    enable_integration_ = false;
  }
  rk2_c_.push_back(Scalar(0.));
  rk2_c_.push_back(Scalar(0.5));
}

template <typename Scalar>
IntegratedActionModelRK2Tpl<Scalar>::~IntegratedActionModelRK2Tpl() {}

template <typename Scalar>
void IntegratedActionModelRK2Tpl<Scalar>::calc(const boost::shared_ptr<ActionDataAbstract>& data,
                                               const Eigen::Ref<const VectorXs>& x,
                                               const Eigen::Ref<const VectorXs>& p) {
  if (static_cast<std::size_t>(x.size()) != state_->get_nx()) {
    throw_pretty("Invalid argument: "
                 << "x has wrong dimension (it should be " + std::to_string(state_->get_nx()) + ")");
  }
  if (static_cast<std::size_t>(p.size()) != nu_) {
    throw_pretty("Invalid argument: "
                 << "u has wrong dimension (it should be " + std::to_string(nu_) + ")");
  }

  const std::size_t nv = differential_->get_state()->get_nv();

  // Static casting the data
  boost::shared_ptr<Data> d = boost::static_pointer_cast<Data>(data);

  // Computing the acceleration and cost
  control_->value(rk2_c_[0], p, d->u_diff[0]);
  differential_->calc(d->differential[0], x, d->u_diff[0]);

  // Computing the next state (discrete time)
  if (enable_integration_) {
    d->y[0] = x;
    d->ki[0].head(nv) = d->y[0].tail(nv);
    d->ki[0].tail(nv) = d->differential[0]->xout;
    d->integral[0] = d->differential[0]->cost;
    
    d->dx_rk2[1] = Scalar(0.5) * time_step_ * d->ki[0];
    differential_->get_state()->integrate(x, d->dx_rk2[1], d->y[1]);
    control_->value(0.5, p, d->u_diff[1]);
    differential_->calc(d->differential[1], d->y[1], d->u_diff[1]);
    d->ki[1].head(nv) = d->y[1].tail(nv);
    d->ki[1].tail(nv) = d->differential[1]->xout;
    d->integral[1] = d->differential[1]->cost;

    d->dx = d->ki[1] * time_step_;
    differential_->get_state()->integrate(x, d->dx, d->xnext);
    d->cost = d->integral[1] * time_step_;
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
void IntegratedActionModelRK2Tpl<Scalar>::calcDiff(const boost::shared_ptr<ActionDataAbstract>& data,
                                                   const Eigen::Ref<const VectorXs>& x,
                                                   const Eigen::Ref<const VectorXs>& p) {
  if (static_cast<std::size_t>(x.size()) != state_->get_nx()) {
    throw_pretty("Invalid argument: "
                 << "x has wrong dimension (it should be " + std::to_string(state_->get_nx()) + ")");
  }
  if (static_cast<std::size_t>(p.size()) != nu_) {
    throw_pretty("Invalid argument: "
                 << "p has wrong dimension (it should be " + std::to_string(nu_) + ")");
  }

  const std::size_t nv = differential_->get_state()->get_nv();

  boost::shared_ptr<Data> d = boost::static_pointer_cast<Data>(data);

  control_->value(0.0, p, d->u_diff[0]);
  differential_->calcDiff(d->differential[0], x, d->u_diff[0]);

  if (enable_integration_) {
    d->dki_dy[0].bottomRows(nv) = d->differential[0]->Fx;
    d->dki_dx[0] = d->dki_dy[0];
    d->dki_dudiff[0].bottomRows(nv) = d->differential[0]->Fu;
    control_->multiplyByDValue(0.0, p, d->dki_dudiff[0], d->dki_du[0]); // dki_du = dki_dudiff * dudiff_du

    d->dli_dx[0] = d->differential[0]->Lx;
    // d->dli_dudiff[0] = d->differential[0]->Lu;
    // control_->multiplyByDValue(0.0, p, d->differential[0]->Lu, d->dli_du[0]); // dli_du = dli_dudiff * dudiff_du
    control_->multiplyDValueTransposeBy(0.0, p, d->differential[0]->Lu, d->dli_du[0]); // dli_du = dli_dudiff * dudiff_du

    d->ddli_ddx[0] = d->differential[0]->Lxx;
    d->ddli_ddudiff[0] = d->differential[0]->Luu;
    control_->multiplyByDValue(0.0, p, d->ddli_ddudiff[0], d->ddli_dudiffdu[0]);          // ddli_dudiffdu = ddli_ddudiff * dudiff_du
    control_->multiplyDValueTransposeBy(0.0, p, d->ddli_dudiffdu[0], d->ddli_ddu[0]); // ddli_ddu = dudiff_du.T * ddli_dudiffdu
    d->ddli_dxdudiff[0] = d->differential[0]->Lxu;
    control_->multiplyByDValue(0.0, p, d->ddli_dxdudiff[0], d->ddli_dxdu[0]);         // ddli_dxdu = ddli_dxdudiff * dudiff_du

    const std::size_t i = 1;
    control_->value(rk2_c_[i], p, d->u_diff[i]);
    differential_->calcDiff(d->differential[i], d->y[i], d->u_diff[i]);
    d->dki_dy[i].bottomRows(nv) = d->differential[i]->Fx;

    d->dyi_dx[i].noalias() = d->dki_dx[i - 1] * rk2_c_[i] * time_step_;
    differential_->get_state()->JintegrateTransport(x, d->dx_rk2[i], d->dyi_dx[i], second);
    differential_->get_state()->Jintegrate(x, d->dx_rk2[i], d->dyi_dx[i], d->dyi_dx[i], first, addto);
    d->dki_dx[i].noalias() = d->dki_dy[i] * d->dyi_dx[i];

    d->dyi_du[i].noalias() = d->dki_du[i - 1] * rk2_c_[i] * time_step_;
    differential_->get_state()->JintegrateTransport(x, d->dx_rk2[i], d->dyi_du[i], second); // dyi_du = Jintegrate * dyi_du
    d->dki_du[i].noalias() = d->dki_dy[i] * d->dyi_du[i]; // TODO: optimize this matrix-matrix multiplication
    d->dki_dudiff[i].bottomRows(nv) = d->differential[i]->Fu;
    control_->multiplyByDValue(rk2_c_[i], p, d->dki_dudiff[i], d->dfi_du[i]); // dfi_du = dki_dudiff * dudiff_du
    d->dki_du[i] += d->dfi_du[i];

    d->dli_dx[i].noalias() = d->differential[i]->Lx.transpose() * d->dyi_dx[i];
    // d->dli_dudiff[i].noalias() = d->differential[i]->Lu.transpose();
    // control_->multiplyByDValue(rk2_c_[i], p, d->differential[i]->Lu.transpose(), d->dli_du[i]); // dli_du = Lu * dudiff_du
    control_->multiplyDValueTransposeBy(rk2_c_[i], p, d->differential[i]->Lu, d->dli_du[i]); // dli_du = Lu * dudiff_du
    d->dli_du[i].noalias() += d->differential[i]->Lx.transpose() * d->dyi_du[i];

    d->Lxx_partialx[i].noalias() = d->differential[i]->Lxx * d->dyi_dx[i];
    d->ddli_ddx[i].noalias() = d->dyi_dx[i].transpose() * d->Lxx_partialx[i];

    control_->multiplyByDValue(rk2_c_[i], p, d->differential[i]->Lxu, d->Lxu_i[i]); // Lxu = Lxudiff * dudiff_du
    d->Luu_partialx[i].noalias() = d->Lxu_i[i].transpose() * d->dyi_du[i];
    d->Lxx_partialu[i].noalias() = d->differential[i]->Lxx * d->dyi_du[i];
    control_->multiplyByDValue(rk2_c_[i], p, d->differential[i]->Luu, d->ddli_dudiffdu[i]); // ddli_dudiffdu = ddli_ddudiff * dudiff_du
    control_->multiplyDValueTransposeBy(rk2_c_[i], p, d->ddli_dudiffdu[i], d->ddli_ddu[i]); // ddli_ddu = dudiff_du.T * ddli_dudiffdu
    d->ddli_ddu[i].noalias() += d->Luu_partialx[i].transpose() + d->Luu_partialx[i] +
                                d->dyi_du[i].transpose() * d->Lxx_partialu[i];

    d->ddli_dxdudiff[i].noalias() = d->dyi_dx[i].transpose() * d->differential[i]->Lxu;
    control_->multiplyByDValue(rk2_c_[i], p, d->ddli_dxdudiff[i], d->ddli_dxdu[i]); // ddli_dxdu = ddli_dxdudiff * dudiff_du
    d->ddli_dxdu[i].noalias() += d->dyi_dx[i].transpose() * d->Lxx_partialu[i];

    d->Fx.noalias() = time_step_ * d->dki_dx[1];
    differential_->get_state()->JintegrateTransport(x, d->dx, d->Fx, second);
    differential_->get_state()->Jintegrate(x, d->dx, d->Fx, d->Fx, first, addto);

    d->Fu.noalias() = time_step_ * d->dki_du[1];
    differential_->get_state()->JintegrateTransport(x, d->dx, d->Fu, second);

    d->Lx.noalias() = time_step_ * d->dli_dx[1];
    d->Lu.noalias() = time_step_ * d->dli_du[1];

    d->Lxx.noalias() = time_step_ * d->ddli_ddx[1];
    d->Luu.noalias() = time_step_ * d->ddli_ddu[1];
    d->Lxu.noalias() = time_step_ * d->ddli_dxdu[1];
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
boost::shared_ptr<ActionDataAbstractTpl<Scalar> > IntegratedActionModelRK2Tpl<Scalar>::createData() {
  return boost::allocate_shared<Data>(Eigen::aligned_allocator<Data>(), this);
}

template <typename Scalar>
bool IntegratedActionModelRK2Tpl<Scalar>::checkData(const boost::shared_ptr<ActionDataAbstract>& data) {
  boost::shared_ptr<Data> d = boost::dynamic_pointer_cast<Data>(data);
  if (data != NULL) {
    return differential_->checkData(d->differential[0]) && differential_->checkData(d->differential[1]);
  } else {
    return false;
  }
}

template <typename Scalar>
void IntegratedActionModelRK2Tpl<Scalar>::quasiStatic(const boost::shared_ptr<ActionDataAbstract>& data,
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
void IntegratedActionModelRK2Tpl<Scalar>::print(std::ostream& os) const {
  os << "IntegratedActionModelRK2 {dt=" << time_step_ << ", " << *differential_ << "}";
}

}  // namespace crocoddyl
