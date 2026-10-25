// RTS Engine — tests/test_actor.cpp
#include "test_framework.h"
#include "sim/entity_store.h"

using namespace rts;

TEST(entity_create_and_lookup) {
	EntityStore store;
	const EntityId a = store.Create(0, SubCoord(100, 200), kCompMovable | kCompHealth);
	const EntityId b = store.Create(1, SubCoord(300, 400), kCompMovable);
	CHECK_EQ(a, 1u);
	CHECK_EQ(b, 2u);
	CHECK_EQ(store.Count(), static_cast<size_t>(2));

	Actor* pa = store.Get(a);
	CHECK(pa != nullptr);
	CHECK_EQ(pa->playerId, 0);
	CHECK_EQ(pa->pos.x, 100);
	CHECK_EQ(pa->pos.y, 200);
	CHECK(pa->Has(kCompMovable));
	CHECK(pa->Has(kCompHealth));
	CHECK(!pa->Has(kCompArmament));

	CHECK(store.Get(999) == nullptr);
}

TEST(entity_store_hash_deterministic) {
	EntityStore a, b;
	a.Create(0, SubCoord(1, 2), kCompMovable);
	a.Create(1, SubCoord(3, 4), kCompMovable | kCompHealth);
	b.Create(0, SubCoord(1, 2), kCompMovable);
	b.Create(1, SubCoord(3, 4), kCompMovable | kCompHealth);
	CHECK_EQ(a.Hash(), b.Hash());

	b.Get(1)->pos.x = 99;
	CHECK_NE(a.Hash(), b.Hash());
}

TEST(entity_clear_resets) {
	EntityStore store;
	store.Create(0, SubCoord(0, 0), kCompMovable);
	store.Clear();
	CHECK_EQ(store.Count(), static_cast<size_t>(0));
	CHECK_EQ(store.NextId(), 1u);
}

TEST(entity_components_flags) {
	Actor a;
	CHECK(!a.Has(kCompArmament));
	a.components = kCompArmament | kCompVision;
	CHECK(a.Has(kCompArmament));
	CHECK(a.Has(kCompVision));
	CHECK(!a.Has(kCompMovable));
}
