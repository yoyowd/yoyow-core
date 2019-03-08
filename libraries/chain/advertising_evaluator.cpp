/*
 * Copyright (c) 2018, YOYOW Foundation PTE. LTD. and contributors.
 */
#include <graphene/chain/advertising_evaluator.hpp>
#include <graphene/chain/advertising_object.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>
#include <graphene/chain/protocol/chain_parameters.hpp>
#include <boost/multiprecision/cpp_int.hpp>

typedef boost::multiprecision::uint128_t uint128_t;

namespace graphene { namespace chain {

void_result advertising_create_evaluator::do_evaluate(const operation_type& op)
{
    try {
        const database& d = db();
        FC_ASSERT(d.head_block_time() >= HARDFORK_0_4_TIME, "Can only create advertising after HARDFORK_0_4_TIME");
        d.get_platform_by_owner(op.platform); // make sure pid exists
        return void_result();
    }FC_CAPTURE_AND_RETHROW((op))
}

object_id_type advertising_create_evaluator::do_apply(const operation_type& op)
{
    try {
        database& d = db();
        const auto& advertising_obj = d.create<advertising_object>([&](advertising_object& obj)
        {
            obj.platform = op.platform;
            obj.on_sell = true;
            obj.unit_time = op.unit_time;
            obj.unit_price = op.unit_price;
            obj.description = op.description;

            obj.publish_time = d.head_block_time();
            obj.last_update_time = d.head_block_time();
        });
        return advertising_obj.id;
    } FC_CAPTURE_AND_RETHROW((op))
}

void_result advertising_update_evaluator::do_evaluate(const operation_type& op)
{
    try {
        const database& d = db();
        FC_ASSERT(d.head_block_time() >= HARDFORK_0_4_TIME, "Can only update advertising after HARDFORK_0_4_TIME");
        d.get_platform_by_owner(op.platform); // make sure pid exists
        advertising_obj = d.find_advertising(op.advertising_id);
        FC_ASSERT(advertising_obj != nullptr, "advertising_object doesn`t exsit");
        FC_ASSERT(advertising_obj->platform == op.platform, "Can`t update other`s advetising. ");

        if (op.on_sell.valid())
            FC_ASSERT(*(op.on_sell) != advertising_obj->on_sell, "advertising state needn`t update. ");
        
        return void_result();

    }FC_CAPTURE_AND_RETHROW((op))
}

void_result advertising_update_evaluator::do_apply(const operation_type& op)
{
    try {
        database& d = db();
        d.modify(*advertising_obj, [&](advertising_object& ad) {
            if (op.description.valid())
                ad.description = *(op.description);
            if (op.unit_price.valid())
                ad.unit_price = *(op.unit_price);
            if (op.unit_time.valid())
                ad.unit_time = *(op.unit_time);
            if (op.on_sell.valid())
                ad.on_sell = *(op.on_sell);
            ad.last_update_time = d.head_block_time();
        });

        return void_result();

    } FC_CAPTURE_AND_RETHROW((op))
}

void_result advertising_buy_evaluator::do_evaluate(const operation_type& op)
{
   try {
      const database& d = db();

      FC_ASSERT(d.head_block_time() >= HARDFORK_0_4_TIME, "Can only buy advertising after HARDFORK_0_4_TIME");
      advertising_obj = d.find_advertising(op.advertising_id);
      FC_ASSERT(advertising_obj != nullptr && advertising_obj->platform == op.platform, 
         "advertising ${tid} on platform ${platform} is invalid.",("tid", op.advertising_id)("platform", op.platform));
      FC_ASSERT(advertising_obj->on_sell, "advertising {id} on platform {platform} not on sell", ("id", op.advertising_id)("platform", op.platform));
      FC_ASSERT(op.start_time >= d.head_block_time(), "start time should be later");

      
      const auto& idx = d.get_index_type<advertising_order_index>().indices().get<by_advertising_id>();
      auto itr = idx.lower_bound(std::make_tuple(advertising_obj->id, true));
      auto itr_end = idx.upper_bound(std::make_tuple(advertising_obj->id, true));

      time_point_sec end_time = op.start_time + advertising_obj->unit_time * op.buy_number;
      while (itr++ != itr_end) {
         if (op.start_time >= itr->end_time || end_time <= itr->start_time)
            continue;
         FC_ASSERT(false, "purchasing date have a conflict, buy advertising failed");
      }

      const auto& from_balance = d.get_balance(op.from_account, GRAPHENE_CORE_ASSET_AID);
      necessary_balance = advertising_obj->unit_price * op.buy_number;
      FC_ASSERT(from_balance.amount >= necessary_balance,
         "Insufficient Balance: ${balance}, not enough to buy advertising ${tid} that ${need} needed.",
         ("need", necessary_balance)("balance", d.to_pretty_string(from_balance)));

      const auto& params = d.get_global_properties().parameters.get_award_params();
      FC_ASSERT(necessary_balance > params.advertising_confirmed_min_fee, 
         "buy price is not enough to pay The lowest poundage ${fee}", ("fee", params.advertising_confirmed_min_fee));

      return void_result();

   }FC_CAPTURE_AND_RETHROW((op))
}

asset advertising_buy_evaluator::do_apply(const operation_type& op)
{
   try {
      database& d = db();

      const auto& advertising_order_obj = d.create<advertising_order_object>([&](advertising_order_object& obj)
      {
         obj.advertising_id = advertising_obj->id;
         obj.user = op.from_account;
         obj.start_time = op.start_time;
         obj.end_time = op.start_time + advertising_obj->unit_time * op.buy_number;
         obj.buy_request_time = d.head_block_time();
         obj.confirmed_status = false;
         obj.released_balance = necessary_balance;
         obj.extra_data = op.extra_data;

         if (op.memo.valid())
            obj.memo = op.memo;
      });

      d.adjust_balance(op.from_account, -asset(necessary_balance));

      return asset(necessary_balance);

   } FC_CAPTURE_AND_RETHROW((op))
}

void_result advertising_confirm_evaluator::do_evaluate(const operation_type& op)
{
   try {
      const database& d = db();

      FC_ASSERT(d.head_block_time() >= HARDFORK_0_4_TIME, "Can only advertising comfirm after HARDFORK_0_4_TIME");
      const auto& advertising_obj = d.find_advertising(op.advertising_id);
      FC_ASSERT(advertising_obj != nullptr && advertising_obj->platform == op.platform,
         "advertising ${tid} on platform ${platform} is invalid.", ("tid", op.advertising_id)("platform", op.platform));
      
      const auto& idx = d.get_index_type<advertising_order_index>().indices().get<by_id>();
      auto itr = idx.find(op.advertising_order_id);
      FC_ASSERT(itr != idx.end(), "order {order} is not existent", ("order", op.advertising_order_id));  

      advertising_order_obj = &(*itr);
      FC_ASSERT(!advertising_order_obj->confirmed_status, 
         "order {order} already effective, should not confirm effective order ", ("order", op.advertising_order_id));

      if (op.iscomfirm) {
         const auto& params = d.get_global_properties().parameters.get_award_params();
         FC_ASSERT(advertising_order_obj->released_balance > params.advertising_confirmed_min_fee,
            "buy price is not enough to pay The lowest poundage ${fee}", ("fee", params.advertising_confirmed_min_fee));
      }

      return void_result();

   }FC_CAPTURE_AND_RETHROW((op))
}

advertising_confirm_result advertising_confirm_evaluator::do_apply(const operation_type& op)
{
   try {
      database& d = db();

      advertising_confirm_result result;
      if (op.iscomfirm)
      {
         d.modify(*advertising_order_obj, [&](advertising_order_object& obj)
         {
            obj.confirmed_status = true;
            obj.released_balance = 0;
         });

         const auto& params = d.get_global_properties().parameters.get_award_params();
         share_type fee = ((uint128_t)(advertising_order_obj->released_balance.value) * params.advertising_confirmed_fee_rate
            / GRAPHENE_100_PERCENT).convert_to<int64_t>();
         if (fee < params.advertising_confirmed_min_fee)
            fee = params.advertising_confirmed_min_fee;

         d.adjust_balance(op.platform, asset(advertising_order_obj->released_balance - fee));
         const auto& core_asset = d.get_core_asset();
         const auto& core_dyn_data = core_asset.dynamic_data(d);
         d.modify(core_dyn_data, [&](asset_dynamic_data_object& dyn)
         {
            dyn.current_supply -= fee;
         });

         result.emplace(advertising_order_obj->user, 0);

         const auto& idx = d.get_index_type<advertising_order_index>().indices().get<by_advertising_id>();
         auto itr = idx.lower_bound(std::make_tuple(op.advertising_id, false));
         auto itr_end = idx.upper_bound(std::make_tuple(op.advertising_id, false));

         while (itr != itr_end)
         {
            if (itr->start_time >= advertising_order_obj->end_time || itr->end_time <= advertising_order_obj->start_time) {
               itr++;
            }
            else
            {
               d.adjust_balance(itr->user, asset(itr->released_balance));
               result.emplace(itr->user, itr->released_balance);
               auto del = itr;
               itr++;
               d.remove(*del);
            }           
         }
      }
      else
      {
         d.adjust_balance(advertising_order_obj->user, asset(advertising_order_obj->released_balance));
         result.emplace(advertising_order_obj->user, advertising_order_obj->released_balance);
         d.remove(*advertising_order_obj);
         advertising_order_obj = nullptr;
      }

      return result;

   } FC_CAPTURE_AND_RETHROW((op))
}

void_result advertising_ransom_evaluator::do_evaluate(const operation_type& op)
{
    try {
        const database& d = db();
        FC_ASSERT(d.head_block_time() >= HARDFORK_0_4_TIME, "Can only ransom advertising after HARDFORK_0_4_TIME");
        d.get_platform_by_owner(op.platform); // make sure pid exists
        d.get_account_by_uid(op.from_account);
        const auto& advertising_obj = d.find_advertising(op.advertising_id);
        FC_ASSERT(advertising_obj != nullptr, "advertising object doesn`t exsit");

        const auto& idx = d.get_index_type<advertising_order_index>().indices().get<by_id>();
        auto itr = idx.find(op.advertising_order_id);
        FC_ASSERT(itr != idx.end(), "order {order} is not existent", ("order", op.advertising_order_id));

        advertising_order_obj = &(*itr);
        FC_ASSERT(advertising_order_obj->user == op.from_account, "your can only ransom your own order. ");
        FC_ASSERT(advertising_order_obj->buy_request_time + GRAPHENE_ADVERTISING_COMFIRM_TIME < d.head_block_time(), 
           "the buy advertising is undetermined. Can`t ransom now.");

        return void_result();

    }FC_CAPTURE_AND_RETHROW((op))
}

void_result advertising_ransom_evaluator::do_apply(const operation_type& op)
{
    try {
        database& d = db();

        d.adjust_balance(op.from_account, asset(advertising_order_obj->released_balance));
        d.remove(*advertising_order_obj);
        advertising_order_obj = nullptr;

    } FC_CAPTURE_AND_RETHROW((op))
}

} } // graphene::chain