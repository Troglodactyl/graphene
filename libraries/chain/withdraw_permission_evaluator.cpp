/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/chain/withdraw_permission_evaluator.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

object_id_type withdraw_permission_create_evaluator::do_evaluate(const operation_type& op)
{ try {
   database& d = db();
   FC_ASSERT(d.find_object(op.withdraw_from_account));
   FC_ASSERT(d.find_object(op.authorized_account));
   FC_ASSERT(d.find_object(op.withdrawal_limit.asset_id));
   FC_ASSERT(op.period_start_time > d.head_block_time());
   FC_ASSERT(op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec > d.head_block_time());
   FC_ASSERT(op.withdrawal_period_sec >= d.get_global_properties().parameters.block_interval);

   return d.get_index_type<withdraw_permission_index>().get_next_id();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type withdraw_permission_create_evaluator::do_apply(const operation_type& op)
{ try {
   return db().create<withdraw_permission_object>([&op](withdraw_permission_object& p) {
      p.withdraw_from_account = op.withdraw_from_account;
      p.authorized_account = op.authorized_account;
      p.withdrawal_limit = op.withdrawal_limit;
      p.withdrawal_period_sec = op.withdrawal_period_sec;
      p.expiration = op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec; 
      p.period_start_time = op.period_start_time;
   }).id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_claim_evaluator::do_evaluate(const withdraw_permission_claim_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = op.withdraw_permission(d);
   FC_ASSERT(permit.expiration > d.head_block_time() );
   FC_ASSERT(permit.authorized_account == op.withdraw_to_account);
   FC_ASSERT(permit.withdraw_from_account == op.withdraw_from_account);
   FC_ASSERT(op.amount_to_withdraw <= permit.available_this_period( d.head_block_time() ) );
   FC_ASSERT(d.get_balance(op.withdraw_from_account, op.amount_to_withdraw.asset_id) >= op.amount_to_withdraw);

   const asset_object& _asset = op.amount_to_withdraw.asset_id(d);
   if( _asset.is_transfer_restricted() ) FC_ASSERT( _asset.issuer == permit.authorized_account || _asset.issuer == permit.withdraw_from_account );

   if( _asset.enforce_white_list() )
   {
      const account_object& from  = op.withdraw_to_account(d);
      const account_object& to    = permit.authorized_account(d);
      FC_ASSERT( to.is_authorized_asset( _asset ) );
      FC_ASSERT( from.is_authorized_asset( _asset ) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_claim_evaluator::do_apply(const withdraw_permission_claim_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = d.get(op.withdraw_permission);
   d.modify(permit, [&](withdraw_permission_object& p) {
      auto periods = (d.head_block_time() - p.period_start_time).to_seconds() / p.withdrawal_period_sec;
      p.period_start_time += periods * p.withdrawal_period_sec;
      if( periods == 0 )
         p.claimed_this_period += op.amount_to_withdraw.amount;
      else
         p.claimed_this_period = op.amount_to_withdraw.amount;
   });

   d.adjust_balance(op.withdraw_from_account, -op.amount_to_withdraw);
   d.adjust_balance(op.withdraw_to_account, op.amount_to_withdraw);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_update_evaluator::do_evaluate(const withdraw_permission_update_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = op.permission_to_update(d);
   FC_ASSERT(permit.authorized_account == op.authorized_account);
   FC_ASSERT(permit.withdraw_from_account == op.withdraw_from_account);
   FC_ASSERT(d.find_object(op.withdrawal_limit.asset_id));
   FC_ASSERT(op.period_start_time >= d.head_block_time());
   FC_ASSERT(op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec > d.head_block_time());
   FC_ASSERT(op.withdrawal_period_sec >= d.get_global_properties().parameters.block_interval);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_update_evaluator::do_apply(const withdraw_permission_update_evaluator::operation_type& op)
{ try {
   database& d = db();

   d.modify(op.permission_to_update(d), [&op](withdraw_permission_object& p) {
      p.period_start_time = op.period_start_time;
      p.expiration = op.period_start_time + op.periods_until_expiration * op.withdrawal_period_sec;
      p.withdrawal_limit = op.withdrawal_limit;
      p.withdrawal_period_sec = op.withdrawal_period_sec;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_delete_evaluator::do_evaluate(const withdraw_permission_delete_evaluator::operation_type& op)
{ try {
   database& d = db();

   const withdraw_permission_object& permit = op.withdrawal_permission(d);
   FC_ASSERT(permit.authorized_account == op.authorized_account);
   FC_ASSERT(permit.withdraw_from_account == op.withdraw_from_account);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result withdraw_permission_delete_evaluator::do_apply(const withdraw_permission_delete_evaluator::operation_type& op)
{
   db().remove(db().get(op.withdrawal_permission));
   return void_result();
}

} } // graphene::chain
