/*
 * Copyright (c) 2018-2022, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Bindings/PrincipalHostDefined.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/ElementFactory.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/Range.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/BrowsingContextGroup.h>
#include <LibWeb/HTML/HTMLDocument.h>
#include <LibWeb/HTML/HTMLInputElement.h>
#include <LibWeb/HTML/SandboxingFlagSet.h>
#include <LibWeb/HTML/Scripting/WindowEnvironmentSettingsObject.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowProxy.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/Layout/Viewport.h>
#include <LibWeb/Namespace.h>
#include <LibWeb/Page/Page.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(BrowsingContext);

// https://html.spec.whatwg.org/multipage/urls-and-fetching.html#matches-about:blank
bool url_matches_about_blank(URL::URL const& url)
{
    // A URL matches about:blank if its scheme is "about", its path contains a single string "blank", its username and password are the empty string, and its host is null.
    return url.scheme() == "about"sv
        && url.paths().size() == 1 && url.paths()[0] == "blank"sv
        && url.username().is_empty()
        && url.password().is_empty()
        && !url.host().has_value();
}

// https://html.spec.whatwg.org/multipage/urls-and-fetching.html#matches-about:srcdoc
bool url_matches_about_srcdoc(URL::URL const& url)
{
    // A URL matches about:srcdoc if its scheme is "about", its path contains a single string "srcdoc", its query is null, its username and password are the empty string, and its host is null.
    return url.scheme() == "about"sv
        && url.paths().size() == 1 && url.paths()[0] == "srcdoc"sv
        && !url.query().has_value()
        && url.username().is_empty()
        && url.password().is_empty()
        && !url.host().has_value();
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#determining-the-origin
URL::Origin determine_the_origin(Optional<URL::URL const&> url, SandboxingFlagSet sandbox_flags, Optional<URL::Origin> source_origin)
{
    // 1. If sandboxFlags has its sandboxed origin browsing context flag set, then return a new opaque origin.
    if (has_flag(sandbox_flags, SandboxingFlagSet::SandboxedOrigin)) {
        return URL::Origin::create_opaque();
    }

    // 2. If url is null, then return a new opaque origin.
    if (!url.has_value()) {
        return URL::Origin::create_opaque();
    }

    // 3. If url is about:srcdoc, then:
    if (url == URL::about_srcdoc()) {
        // 1. Assert: sourceOrigin is non-null.
        VERIFY(source_origin.has_value());

        // 2. Return sourceOrigin.
        return source_origin.release_value();
    }

    // 4. If url matches about:blank and sourceOrigin is non-null, then return sourceOrigin.
    if (url_matches_about_blank(*url) && source_origin.has_value())
        return source_origin.release_value();

    // 5. Return url's origin.
    return url->origin();
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#creating-a-new-auxiliary-browsing-context
WebIDL::ExceptionOr<BrowsingContext::BrowsingContextAndDocument> BrowsingContext::create_a_new_auxiliary_browsing_context_and_document(GC::Ref<Page> page, GC::Ref<HTML::BrowsingContext> opener)
{
    // 1. Let openerTopLevelBrowsingContext be opener's top-level traversable's active browsing context.
    auto opener_top_level_browsing_context = opener->top_level_traversable()->active_browsing_context();

    // 2. Let group be openerTopLevelBrowsingContext's group.
    auto group = opener_top_level_browsing_context->group();

    // 3. Assert: group is non-null, as navigating invokes this directly.
    VERIFY(group);

    // 4. Set browsingContext and document be the result of creating a new browsing context and document with opener's active document, null, and group.
    auto [browsing_context, document] = TRY(create_a_new_browsing_context_and_document(page, opener->active_document(), nullptr, *group));

    // 5. Set browsingContext's is auxiliary to true.
    browsing_context->m_is_auxiliary = true;

    // 6. Append browsingContext to group.
    group->append(browsing_context);

    // 7. Set browsingContext's opener browsing context to opener.
    browsing_context->set_opener_browsing_context(opener);

    // 8. Set browsingContext's virtual browsing context group ID to openerTopLevelBrowsingContext's virtual browsing context group ID.
    browsing_context->m_virtual_browsing_context_group_id = opener_top_level_browsing_context->m_virtual_browsing_context_group_id;

    // 9. Set browsingContext's opener origin at creation to opener's active document's origin.
    browsing_context->m_opener_origin_at_creation = opener->active_document()->origin();

    // 10. Return browsingContext and document.
    return BrowsingContext::BrowsingContextAndDocument { browsing_context, document };
}

static void populate_with_html_head_body(GC::Ref<DOM::Document> document)
{
    auto html_node = MUST(DOM::create_element(document, HTML::TagNames::html, Namespace::HTML));
    auto head_element = MUST(DOM::create_element(document, HTML::TagNames::head, Namespace::HTML));
    MUST(html_node->append_child(head_element));
    auto body_element = MUST(DOM::create_element(document, HTML::TagNames::body, Namespace::HTML));
    MUST(html_node->append_child(body_element));
    MUST(document->append_child(html_node));
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#creating-a-new-browsing-context
WebIDL::ExceptionOr<BrowsingContext::BrowsingContextAndDocument> BrowsingContext::create_a_new_browsing_context_and_document(GC::Ref<Page> page, GC::Ptr<DOM::Document> creator, GC::Ptr<DOM::Element> embedder, GC::Ref<BrowsingContextGroup> group)
{
    auto& vm = group->vm();

    // 1. Let browsingContext be a new browsing context.
    GC::Ref<BrowsingContext> browsing_context = *vm.heap().allocate<BrowsingContext>(page);

    // 2. Let unsafeContextCreationTime be the unsafe shared current time.
    [[maybe_unused]] auto unsafe_context_creation_time = HighResolutionTime::unsafe_shared_current_time();

    // 3. Let creatorOrigin be null.
    Optional<URL::Origin> creator_origin = {};

    // 4. Let creatorBaseURL be null.
    Optional<URL::URL> creator_base_url = {};

    // 5. If creator is non-null, then:
    if (creator) {
        // 1. Set creatorOrigin to creator's origin.
        creator_origin = creator->origin();

        // 2. Set creatorBaseURL to creator's document base URL.
        creator_base_url = creator->base_url();

        // 3. Set browsingContext's virtual browsing context group ID to creator's browsing context's top-level browsing context's virtual browsing context group ID.
        VERIFY(creator->browsing_context());
        browsing_context->m_virtual_browsing_context_group_id = creator->browsing_context()->top_level_browsing_context()->m_virtual_browsing_context_group_id;
    }

    // FIXME: 6. Let sandboxFlags be the result of determining the creation sandboxing flags given browsingContext and embedder.
    SandboxingFlagSet sandbox_flags = {};

    // 7. Let origin be the result of determining the origin given about:blank, sandboxFlags, and creatorOrigin.
    auto origin = determine_the_origin(URL::about_blank(), sandbox_flags, creator_origin);

    // FIXME: 8. Let permissionsPolicy be the result of creating a permissions policy given embedder and origin. [PERMISSIONSPOLICY]

    // FIXME: 9. Let agent be the result of obtaining a similar-origin window agent given origin, group, and false.

    GC::Ptr<Window> window;

    // 10. Let realm execution context be the result of creating a new JavaScript realm given agent and the following customizations:
    auto realm_execution_context = Bindings::create_a_new_javascript_realm(
        Bindings::main_thread_vm(),
        [&](JS::Realm& realm) -> JS::Object* {
            auto window_proxy = realm.create<WindowProxy>(realm);
            browsing_context->set_window_proxy(window_proxy);

            // - For the global object, create a new Window object.
            window = Window::create(realm);
            return window.ptr();
        },
        [&](JS::Realm&) -> JS::Object* {
            // - For the global this binding, use browsingContext's WindowProxy object.
            return browsing_context->window_proxy();
        });

    // 11. Let topLevelCreationURL be about:blank if embedder is null; otherwise embedder's relevant settings object's top-level creation URL.
    auto top_level_creation_url = !embedder ? URL::about_blank() : relevant_settings_object(*embedder).top_level_creation_url.value();

    // 12. Let topLevelOrigin be origin if embedder is null; otherwise embedder's relevant settings object's top-level origin.
    auto top_level_origin = !embedder ? origin : relevant_settings_object(*embedder).origin();

    // 13. Set up a window environment settings object with about:blank, realm execution context, null, topLevelCreationURL, and topLevelOrigin.
    WindowEnvironmentSettingsObject::setup(
        page,
        URL::about_blank(),
        move(realm_execution_context),
        {},
        top_level_creation_url,
        top_level_origin);

    // 14. Let loadTimingInfo be a new document load timing info with its navigation start time set to the result of calling
    //     coarsen time with unsafeContextCreationTime and the new environment settings object's cross-origin isolated capability.
    auto load_timing_info = DOM::DocumentLoadTimingInfo();
    load_timing_info.navigation_start_time = HighResolutionTime::coarsen_time(
        unsafe_context_creation_time,
        as<WindowEnvironmentSettingsObject>(Bindings::principal_host_defined_environment_settings_object(window->realm())).cross_origin_isolated_capability() == CanUseCrossOriginIsolatedAPIs::Yes);

    // 15. Let document be a new Document, with:
    auto document = HTML::HTMLDocument::create(window->realm());

    // Non-standard
    window->set_associated_document(*document);

    // type: "html"
    document->set_document_type(DOM::Document::Type::HTML);

    // content type: "text/html"
    document->set_content_type("text/html"_string);

    // mode: "quirks"
    document->set_quirks_mode(DOM::QuirksMode::Yes);

    // origin: origin
    document->set_origin(origin);

    // browsing context: browsingContext
    document->set_browsing_context(browsing_context);

    // FIXME: permissions policy: permissionsPolicy

    // active sandboxing flag set: sandboxFlags
    document->set_active_sandboxing_flag_set(sandbox_flags);

    // load timing info: loadTimingInfo
    document->set_load_timing_info(load_timing_info);

    // is initial about:blank: true
    document->set_is_initial_about_blank(true);
    // Spec issue: https://github.com/whatwg/html/issues/10261
    document->set_ready_to_run_scripts();

    // about base URL: creatorBaseURL
    document->set_about_base_url(creator_base_url);

    // allow declarative shadow roots: true
    document->set_allow_declarative_shadow_roots(true);

    // 16. If creator is non-null, then:
    if (creator) {
        // 1. Set document's referrer to the serialization of creator's URL.
        document->set_referrer(creator->url().serialize());

        // 2. Set document's policy container to a clone of creator's policy container.
        document->set_policy_container(creator->policy_container()->clone(document->heap()));

        // 3. If creator's origin is same origin with creator's relevant settings object's top-level origin,
        if (creator->origin().is_same_origin(creator->relevant_settings_object().top_level_origin.value())) {
            // then set document's opener policy to creator's browsing context's top-level browsing context's active document's opener policy.
            VERIFY(creator->browsing_context());
            VERIFY(creator->browsing_context()->top_level_browsing_context()->active_document());
            document->set_opener_policy(creator->browsing_context()->top_level_browsing_context()->active_document()->opener_policy());
        }
    }

    // 17. Assert: document's URL and document's relevant settings object's creation URL are about:blank.
    VERIFY(document->url() == URL::about_blank());
    VERIFY(document->relevant_settings_object().creation_url == URL::about_blank());

    // 18. Mark document as ready for post-load tasks.
    document->set_ready_for_post_load_tasks(true);

    // 19. Populate with html/head/body given document.
    populate_with_html_head_body(*document);

    // 20. Make active document.
    document->make_active();

    // 21. Completely finish loading document.
    document->completely_finish_loading();

    // 22. Return browsingContext and document.
    return BrowsingContext::BrowsingContextAndDocument { browsing_context, document };
}

BrowsingContext::BrowsingContext(GC::Ref<Page> page)
    : m_page(page)
{
}

BrowsingContext::~BrowsingContext() = default;

void BrowsingContext::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);

    visitor.visit(m_page);
    visitor.visit(m_window_proxy);
    visitor.visit(m_group);
    visitor.visit(m_first_child);
    visitor.visit(m_last_child);
    visitor.visit(m_next_sibling);
    visitor.visit(m_previous_sibling);
    visitor.visit(m_opener_browsing_context);
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#bc-traversable
GC::Ref<TraversableNavigable> BrowsingContext::top_level_traversable() const
{
    // A browsing context's top-level traversable is its active document's node navigable's top-level traversable.
    auto traversable = active_document()->navigable()->top_level_traversable();
    VERIFY(traversable);
    VERIFY(traversable->is_top_level_traversable());
    return *traversable;
}

// https://html.spec.whatwg.org/multipage/browsers.html#top-level-browsing-context
bool BrowsingContext::is_top_level() const
{
    // FIXME: Remove this. The active document's navigable is sometimes null when it shouldn't be, failing assertions.
    return true;
    // A top-level browsing context is a browsing context whose active document's node navigable is a traversable navigable.
    return active_document() != nullptr && active_document()->navigable() != nullptr && active_document()->navigable()->is_traversable();
}

GC::Ptr<BrowsingContext> BrowsingContext::top_level_browsing_context() const
{
    auto const* start = this;

    // 1. If start's active document is not fully active, then return null.
    if (!start->active_document()->is_fully_active()) {
        return nullptr;
    }

    // 2. Let navigable be start's active document's node navigable.
    auto navigable = start->active_document()->navigable();

    // 3. While navigable's parent is not null, set navigable to navigable's parent.
    while (navigable->parent()) {
        navigable = navigable->parent();
    }

    // 4. Return navigable's active browsing context.
    return navigable->active_browsing_context();
}

DOM::Document const* BrowsingContext::active_document() const
{
    auto* window = active_window();
    if (!window)
        return nullptr;
    return &window->associated_document();
}

DOM::Document* BrowsingContext::active_document()
{
    auto* window = active_window();
    if (!window)
        return nullptr;
    return &window->associated_document();
}

// https://html.spec.whatwg.org/multipage/browsers.html#active-window
HTML::Window* BrowsingContext::active_window()
{
    return m_window_proxy->window();
}

// https://html.spec.whatwg.org/multipage/browsers.html#active-window
HTML::Window const* BrowsingContext::active_window() const
{
    return m_window_proxy->window();
}

HTML::WindowProxy* BrowsingContext::window_proxy()
{
    return m_window_proxy.ptr();
}

HTML::WindowProxy const* BrowsingContext::window_proxy() const
{
    return m_window_proxy.ptr();
}

void BrowsingContext::set_window_proxy(GC::Ptr<WindowProxy> window_proxy)
{
    m_window_proxy = move(window_proxy);
}

BrowsingContextGroup* BrowsingContext::group()
{
    return m_group;
}

BrowsingContextGroup const* BrowsingContext::group() const
{
    return m_group;
}

void BrowsingContext::set_group(BrowsingContextGroup* group)
{
    m_group = group;
}

// https://html.spec.whatwg.org/multipage/browsers.html#bcg-remove
void BrowsingContext::remove()
{
    // 1. Assert: browsingContext's group is non-null, because a browsing context only gets discarded once.
    VERIFY(group());

    // 2. Let group be browsingContext's group.
    GC::Ref<BrowsingContextGroup> group = *this->group();

    // 3. Set browsingContext's group to null.
    set_group(nullptr);

    // 4. Remove browsingContext from group's browsing context set.
    group->browsing_context_set().remove(*this);

    // 5. If group's browsing context set is empty, then remove group from the user agent's browsing context group set.
    // NOTE: This is done by ~BrowsingContextGroup() when the refcount reaches 0.
}

// https://html.spec.whatwg.org/multipage/origin.html#one-permitted-sandboxed-navigator
BrowsingContext const* BrowsingContext::the_one_permitted_sandboxed_navigator() const
{
    // FIXME: Implement this.
    return nullptr;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#ancestor-browsing-context
bool BrowsingContext::is_ancestor_of(BrowsingContext const& potential_descendant) const
{
    // A browsing context potentialDescendant is said to be an ancestor of a browsing context potentialAncestor if the following algorithm returns true:

    // 1. Let potentialDescendantDocument be potentialDescendant's active document.
    auto const* potential_descendant_document = potential_descendant.active_document();

    // 2. If potentialDescendantDocument is not fully active, then return false.
    if (!potential_descendant_document->is_fully_active())
        return false;

    // 3. Let ancestorBCs be the list obtained by taking the browsing context of the active document of each member of potentialDescendantDocument's ancestor navigables.
    for (auto const& ancestor : potential_descendant_document->ancestor_navigables()) {
        auto ancestor_browsing_context = ancestor->active_browsing_context();

        // 4. If ancestorBCs contains potentialAncestor, then return true.
        if (ancestor_browsing_context == this)
            return true;
    }

    // 5. Return false.
    return false;
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#familiar-with
bool BrowsingContext::is_familiar_with(BrowsingContext const& other) const
{
    // A browsing context A is familiar with a second browsing context B if the following algorithm returns true:
    auto const& A = *this;
    auto const& B = other;

    // 1. If A's active document's origin is same origin with B's active document's origin, then return true.
    if (A.active_document()->origin().is_same_origin(B.active_document()->origin()))
        return true;

    // 2. If A's top-level browsing context is B, then return true.
    if (A.top_level_browsing_context() == &B)
        return true;

    // 3. If B is an auxiliary browsing context and A is familiar with B's opener browsing context, then return true.
    if (B.opener_browsing_context() != nullptr && A.is_familiar_with(*B.opener_browsing_context()))
        return true;

    // 4. If there exists an ancestor browsing context of B whose active document has the same origin as the active document of A, then return true.
    // NOTE: This includes the case where A is an ancestor browsing context of B.

    // If B's active document is not fully active then it cannot have ancestor browsing context
    if (!B.active_document()->is_fully_active())
        return false;

    for (auto const& ancestor : B.active_document()->ancestor_navigables()) {
        if (ancestor->active_document()->origin().is_same_origin(A.active_document()->origin()))
            return true;
    }

    // 5. Return false.
    return false;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#snapshotting-target-snapshot-params
SandboxingFlagSet determine_the_creation_sandboxing_flags(BrowsingContext const&, GC::Ptr<DOM::Element>)
{
    // FIXME: Populate this once we have the proper flag sets on BrowsingContext
    return {};
}

bool BrowsingContext::has_navigable_been_destroyed() const
{
    auto navigable = active_document()->navigable();
    return !navigable || navigable->has_been_destroyed();
}

}
