// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <algorithm>
#include <string>
#include "folly/Conv.h"

#include <fboss/thrift_cow/nodes/Serializer.h>
#include <fboss/thrift_cow/visitors/TraverseHelper.h>
#include <fboss/thrift_cow/visitors/VisitorUtils.h>
#include <folly/Traits.h>
#include <thrift/lib/cpp2/Thrift.h>
#include <thrift/lib/cpp2/TypeClass.h>
#include <thrift/lib/cpp2/reflection/reflection.h>

namespace facebook::fboss::thrift_cow {

struct HybridNodeType;

enum class RecurseVisitMode {
  /*
   * In this mode, we visit every path in the tree, including
   * intermediate nodes.
   */
  FULL,

  /*
   * In this mode, we only visit leaf nodes.
   */
  LEAVES,

  /*
   * Similar to FULL, but we only visit unpublished nodes.
   */
  UNPUBLISHED
};

enum class RecurseVisitOrder {
  PARENTS_FIRST,

  CHILDREN_FIRST
};

struct RecurseVisitOptions {
  RecurseVisitOptions(
      RecurseVisitMode mode,
      RecurseVisitOrder order,
      bool outputIdPaths = false,
      bool hybridNodeShallowTraversal = false,
      bool hybridNodeDeepTraversal = false)
      : mode(mode),
        order(order),
        outputIdPaths(outputIdPaths),
        hybridNodeShallowTraversal(hybridNodeShallowTraversal),
        hybridNodeDeepTraversal(hybridNodeDeepTraversal) {}
  RecurseVisitMode mode;
  RecurseVisitOrder order;
  bool outputIdPaths;
  bool hybridNodeShallowTraversal;
  bool hybridNodeDeepTraversal;
};

template <typename>
struct RecurseVisitor;

struct NodeType;
struct FieldsType;

namespace rv_detail {

/*
 * invokeVisitorFnHelper allows us to support different visitor signatures
 * if needed.
 */

template <typename Node, typename TraverseHelper, typename Func>
void invokeVisitorFnHelper(
    RecurseVisitOptions /* options */,
    TraverseHelper& traverser,
    Node&& node,
    Func&& f) {
  return f(traverser, std::forward<Node>(node));
}

template <typename TC, typename TType, typename TraverseHelper, typename Func>
void invokeVisitorFnWithWrapper(
    RecurseVisitOptions /* options */,
    TraverseHelper& traverser,
    TType& node,
    Func&& f) {
  std::optional<SerializableWrapper<TC, TType>> wrapper =
      SerializableWrapper<TC, TType>(node);
  return f(traverser, wrapper);
}

template <typename TC, typename TType, typename TraverseHelper, typename Func>
void invokeVisitorFnWithWrapper(
    RecurseVisitOptions /* options */,
    TraverseHelper& traverser,
    const TType& node,
    Func&& f) {
  std::optional<SerializableWrapper<TC, TType>> wrapper =
      SerializableWrapper<TC, TType>(*const_cast<TType*>(&node));
  return f(traverser, wrapper);
}

template <typename TC, typename NodePtr, typename TraverseHelper, typename Func>
void visitNode(
    TraverseHelper& traverser,
    NodePtr& node,
    RecurseVisitOptions options,
    Func&& f)
    // only enable for Node types
  requires(std::is_same_v<
           typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
           NodeType>)
{
  if (options.mode == RecurseVisitMode::UNPUBLISHED && node->isPublished()) {
    return;
  }

  if (traverser.shouldShortCircuit(VisitorType::RECURSE)) {
    return;
  }

  bool visitIntermediate = options.mode == RecurseVisitMode::FULL ||
      options.mode == RecurseVisitMode::UNPUBLISHED;
  if (visitIntermediate && options.order == RecurseVisitOrder::PARENTS_FIRST) {
    invokeVisitorFnHelper(options, traverser, node, std::forward<Func>(f));
  }

  if constexpr (std::is_const_v<NodePtr>) {
    RecurseVisitor<TC>::visit(
        traverser, *node->getFields(), options, std::forward<Func>(f));
  } else {
    RecurseVisitor<TC>::visit(
        traverser, *node->writableFields(), options, std::forward<Func>(f));
  }

  if (visitIntermediate && options.order == RecurseVisitOrder::CHILDREN_FIRST) {
    invokeVisitorFnHelper(options, traverser, node, std::forward<Func>(f));
  }
}

} // namespace rv_detail

/**
 * Set
 */
template <typename ValueTypeClass>
struct RecurseVisitor<apache::thrift::type_class::set<ValueTypeClass>> {
  using TC = apache::thrift::type_class::set<ValueTypeClass>;
  template <typename NodePtr, typename TraverseHelper, typename Func>
  static inline void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Node types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             NodeType>)
  {
    return rv_detail::visitNode<TC>(
        traverser, node, options, std::forward<Func>(f));
  }

  template <typename NodePtr, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for HybridNode types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             HybridNodeType>)
  {
    if (options.hybridNodeDeepTraversal) {
      throw std::runtime_error(folly::to<std::string>(
          "RecurseVisitor support for hybridNodeDeepTraversal in Set not implemented"));
    }
    rv_detail::invokeVisitorFnHelper(
        options, traverser, node, std::forward<Func>(f));
  }

  template <typename Fields, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      Fields& fields,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Fields types
    requires(std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    for (auto& val : fields) {
      traverser.push(
          folly::to<std::string>(val->cref()), TCType<ValueTypeClass>);
      rv_detail::invokeVisitorFnHelper(
          options,
          traverser,
          typename Fields::value_type{val},
          std::forward<Func>(f));
      traverser.pop(TCType<ValueTypeClass>);
    }
  }
};

/**
 * List
 */
template <typename ValueTypeClass>
struct RecurseVisitor<apache::thrift::type_class::list<ValueTypeClass>> {
  using TC = apache::thrift::type_class::list<ValueTypeClass>;
  template <typename NodePtr, typename TraverseHelper, typename Func>
  static inline void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Node types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             NodeType>)
  {
    return rv_detail::visitNode<TC>(
        traverser, node, options, std::forward<Func>(f));
  }

  template <typename NodePtr, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for HybridNode types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             HybridNodeType>)
  {
    if (options.hybridNodeDeepTraversal) {
      throw std::runtime_error(folly::to<std::string>(
          "RecurseVisitor support for hybridNodeDeepTraversal in List not implemented"));
    }
    if (options.mode == RecurseVisitMode::UNPUBLISHED) {
      // unpublished hybrid node, no children to be published
      rv_detail::invokeVisitorFnHelper(
          options, traverser, node, std::forward<Func>(f));
      return;
    }
    auto& tObj = node->ref();
    bool visitIntermediate = (options.mode == RecurseVisitMode::FULL);
    if (visitIntermediate &&
        options.order == RecurseVisitOrder::PARENTS_FIRST) {
      rv_detail::invokeVisitorFnHelper(
          options, traverser, node, std::forward<Func>(f));
    }
    if (options.hybridNodeShallowTraversal) {
      // visit list elements
      for (int i = 0; i < tObj.size(); ++i) {
        traverser.push(folly::to<std::string>(i), TCType<ValueTypeClass>);
        rv_detail::invokeVisitorFnWithWrapper<ValueTypeClass>(
            options, traverser, tObj.at(i), std::forward<Func>(f));
        traverser.pop(TCType<ValueTypeClass>);
      }
    }
    if (visitIntermediate &&
        options.order == RecurseVisitOrder::CHILDREN_FIRST) {
      rv_detail::invokeVisitorFnHelper(
          options, traverser, node, std::forward<Func>(f));
    }
  }

  template <typename Fields, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      Fields& fields,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Fields types
    requires(std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    for (int i = 0; i < fields.size(); ++i) {
      traverser.push(folly::to<std::string>(i), TCType<ValueTypeClass>);
      RecurseVisitor<ValueTypeClass>::visit(
          traverser, fields.ref(i), options, std::forward<Func>(f));
      traverser.pop(TCType<ValueTypeClass>);
    }
  }
};

/**
 * Map
 */
template <typename KeyTypeClass, typename MappedTypeClass>
struct RecurseVisitor<
    apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>> {
  using TC = apache::thrift::type_class::map<KeyTypeClass, MappedTypeClass>;
  template <typename NodePtr, typename TraverseHelper, typename Func>
  static inline void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Node types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             NodeType>)
  {
    return rv_detail::visitNode<TC>(
        traverser, node, options, std::forward<Func>(f));
  }

  template <typename NodePtr, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for HybridNode types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             HybridNodeType>)
  {
    if (options.hybridNodeDeepTraversal) {
      throw std::runtime_error(folly::to<std::string>(
          "RecurseVisitor support for hybridNodeDeepTraversal in Map not implemented"));
    }
    if (options.mode == RecurseVisitMode::UNPUBLISHED) {
      rv_detail::invokeVisitorFnHelper(
          options, traverser, node, std::forward<Func>(f));
      return;
    }
    auto& tObj = node->ref();
    bool visitIntermediate = options.mode == RecurseVisitMode::FULL;
    if (visitIntermediate &&
        options.order == RecurseVisitOrder::PARENTS_FIRST) {
      rv_detail::invokeVisitorFnHelper(
          options, traverser, node, std::forward<Func>(f));
    }
    // visit map entries
    if (options.hybridNodeShallowTraversal) {
      if constexpr (std::is_const_v<NodePtr>) {
        for (const auto& [key, val] : tObj) {
          traverser.push(folly::to<std::string>(key), TCType<MappedTypeClass>);
          rv_detail::invokeVisitorFnWithWrapper<MappedTypeClass>(
              options, traverser, val, std::forward<Func>(f));
          traverser.pop(TCType<MappedTypeClass>);
        }
      } else {
        for (auto& [key, val] : tObj) {
          traverser.push(folly::to<std::string>(key), TCType<MappedTypeClass>);
          rv_detail::invokeVisitorFnWithWrapper<MappedTypeClass>(
              options, traverser, val, std::forward<Func>(f));
          traverser.pop(TCType<MappedTypeClass>);
        }
      }
    }
    if (visitIntermediate &&
        options.order == RecurseVisitOrder::CHILDREN_FIRST) {
      rv_detail::invokeVisitorFnHelper(
          options, traverser, node, std::forward<Func>(f));
    }
  }

  template <typename Fields, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      Fields& fields,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Fields types
    requires(std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    if constexpr (std::is_const_v<Fields>) {
      for (const auto& [key, val] : fields) {
        traverser.push(folly::to<std::string>(key), TCType<MappedTypeClass>);
        RecurseVisitor<MappedTypeClass>::visit(
            traverser, val, options, std::forward<Func>(f));
        traverser.pop(TCType<MappedTypeClass>);
      }
    } else {
      for (auto& [key, val] : fields) {
        traverser.push(folly::to<std::string>(key), TCType<MappedTypeClass>);
        RecurseVisitor<MappedTypeClass>::visit(
            traverser, val, options, std::forward<Func>(f));
        traverser.pop(TCType<MappedTypeClass>);
      }
    }
  }
};

/**
 * Variant
 */
template <>
struct RecurseVisitor<apache::thrift::type_class::variant> {
  using TC = apache::thrift::type_class::variant;
  template <typename NodePtr, typename TraverseHelper, typename Func>
  static inline void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Node types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             NodeType>)
  {
    return rv_detail::visitNode<TC>(
        traverser, node, options, std::forward<Func>(f));
  }

  template <typename NodePtr, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for HybridNode types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             HybridNodeType>)
  {
    if (options.hybridNodeDeepTraversal) {
      throw std::runtime_error(folly::to<std::string>(
          "RecurseVisitor support for hybridNodeDeepTraversal in Variant not implemented"));
    }
    rv_detail::invokeVisitorFnHelper(
        options, traverser, node, std::forward<Func>(f));
  }

  template <typename Fields, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      Fields& fields,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Fields types
    requires(std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    using Members = typename Fields::Members;

    fatal::scalar_search<Members, fatal::get_type::id>(
        fields.type(), [&](auto indexed) {
          using descriptor = decltype(fatal::tag_type(indexed));
          using name = typename descriptor::metadata::name;
          using tc = typename descriptor::metadata::type_class;

          std::string memberName = getMemberName<typename descriptor::metadata>(
              options.outputIdPaths);

          traverser.push(std::move(memberName), TCType<tc>);

          if constexpr (std::is_const_v<Fields>) {
            const auto& ref = fields.template cref<name>();
            RecurseVisitor<tc>::visit(
                traverser, ref, options, std::forward<Func>(f));
          } else {
            auto& ref = fields.template ref<name>();
            RecurseVisitor<tc>::visit(
                traverser, ref, options, std::forward<Func>(f));
          }
          traverser.pop(TCType<tc>);
        });
  }
};

/**
 * Structure
 */
template <>
struct RecurseVisitor<apache::thrift::type_class::structure> {
  using TC = apache::thrift::type_class::structure;
  template <typename NodePtr, typename Func>
  static void visit(NodePtr& node, RecurseVisitOptions options, Func&& f) {
    SimpleTraverseHelper traverser;
    return visit(traverser, node, options, std::forward<Func>(f));
  }

  template <typename NodePtr, typename TraverseHelper, typename Func>
  static inline void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Node types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             NodeType>)
  {
    return rv_detail::visitNode<TC>(
        traverser, node, options, std::forward<Func>(f));
  }

  template <typename NodePtr, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      NodePtr& node,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for HybridNode types
    requires(std::is_same_v<
             typename folly::remove_cvref_t<NodePtr>::element_type::CowType,
             HybridNodeType>)
  {
    if (options.hybridNodeDeepTraversal) {
      throw std::runtime_error(folly::to<std::string>(
          "RecurseVisitor support for hybridNodeDeepTraversal in Struct not implemented"));
    }
    rv_detail::invokeVisitorFnHelper(
        options, traverser, node, std::forward<Func>(f));
  }

  template <typename Fields, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      Fields& fields,
      RecurseVisitOptions options,
      Func&& f)
      // only enable for Fields types
    requires(std::is_same_v<typename Fields::CowType, FieldsType>)
  {
    using Members = typename Fields::Members;

    fatal::foreach<Members>([&](auto indexed) {
      using member = decltype(fatal::tag_type(indexed));
      using name = typename member::name;
      using tc = typename member::type_class;

      // Look for the expected member name
      std::string memberName = getMemberName<member>(options.outputIdPaths);

      traverser.push(std::move(memberName), TCType<tc>);

      if constexpr (std::is_const_v<Fields>) {
        const auto& ref = fields.template cref<name>();
        if (ref) {
          RecurseVisitor<tc>::visit(
              traverser, ref, options, std::forward<Func>(f));
        }
      } else {
        auto& ref = fields.template ref<name>();
        if (ref) {
          RecurseVisitor<tc>::visit(
              traverser, ref, options, std::forward<Func>(f));
        }
      }
      traverser.pop(TCType<tc>);
    });
  }
};

/**
 * Primitives - fallback specialization
 * - string / binary
 * - floating_point
 * - integral
 * - enumeration
 */
template <typename TC>
struct RecurseVisitor {
  static_assert(
      !std::is_same<apache::thrift::type_class::unknown, TC>::value,
      "No static reflection support for the given type. "
      "Forgot to specify reflection option or include fatal header file? "
      "Refer to thrift/lib/cpp2/reflection/reflection.h");

  template <typename Fields, typename TraverseHelper, typename Func>
  static void visit(
      TraverseHelper& traverser,
      Fields& fields,
      RecurseVisitOptions options,
      Func&& f) {
    rv_detail::invokeVisitorFnHelper(
        options, traverser, fields, std::forward<Func>(f));
  }
};

using RootRecurseVisitor =
    RecurseVisitor<apache::thrift::type_class::structure>;

} // namespace facebook::fboss::thrift_cow
