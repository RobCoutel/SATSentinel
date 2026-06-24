#include <catch2/catch_all.hpp>

#include <set>
#include <string>
#include <vector>


#define private public
#include "Sentinel-types.hpp"
#undef private

using namespace sentinel;

TEST_CASE("variable test", "[sentinel-types]")
{
  Tvar v1{1};
  Tvar v2{2};
  Tvar v3{1};

  REQUIRE(v1 == v3);
  REQUIRE(v1 != v2);
  REQUIRE(v1 < v2);
  REQUIRE(v2 > v1);

  REQUIRE(v1.to_string() == "v1");
  REQUIRE(v2.to_string() == "v2");
}


TEST_CASE("literal test", "[sentinel-types]")
{
  Tvar v1{1};
  Tvar v2{2};

  Tlit l1{v1, 0};
  Tlit l2{v1, 1};
  Tlit l3{v2, 0};
  Tlit l4{v2, 1};

  REQUIRE(l1 != l2);
  REQUIRE(l1 != l3);
  REQUIRE(l1 != l4);
  REQUIRE(l2 != l3);
  REQUIRE(l2 != l4);
  REQUIRE(l3 != l4);

  REQUIRE(l1.var() == v1);
  REQUIRE(l2.var() == v1);
  REQUIRE(l3.var() == v2);
  REQUIRE(l4.var() == v2);

  REQUIRE(!l1.pol());
  REQUIRE(l2.pol());
  REQUIRE(!l3.pol());
  REQUIRE(l4.pol());

  REQUIRE(~l1 == l2);
  REQUIRE(~l2 == l1);
  REQUIRE(~l3 == l4);
  REQUIRE(~l4 == l3);

  REQUIRE(l1.to_string() == "~1");
  REQUIRE(l2.to_string() == "1");
  REQUIRE(l3.to_string() == "~2");
  REQUIRE(l4.to_string() == "2");
}

TEST_CASE("literal satisfied, falsified, and undefined tests", "[sentinel-types]")
{
  Tvar v1{1};
  Tvar v2{2};

  Tlit l1{v1, 0}; // ~v1
  Tlit l2{v1, 1}; // v1
  Tlit l3{v2, 0}; // ~v2
  Tlit l4{v2, 1}; // v2

  Tval val_true{VAR_TRUE};
  Tval val_false{VAR_FALSE};
  Tval val_undef{VAR_UNDEF};

  REQUIRE(l1.satisfied(val_false));
  REQUIRE(!l1.satisfied(val_true));
  REQUIRE(!l1.satisfied(val_undef));

  REQUIRE(!l1.falsified(val_false));
  REQUIRE(l1.falsified(val_true));
  REQUIRE(!l1.falsified(val_undef));

  REQUIRE(!l1.undefined(val_false));
  REQUIRE(!l1.undefined(val_true));
  REQUIRE(l1.undefined(val_undef));


  REQUIRE(!l2.satisfied(val_false));
  REQUIRE(l2.satisfied(val_true));
  REQUIRE(!l2.satisfied(val_undef));

  REQUIRE(l2.falsified(val_false));
  REQUIRE(!l2.falsified(val_true));
  REQUIRE(!l2.falsified(val_undef));

  REQUIRE(!l2.undefined(val_false));
  REQUIRE(!l2.undefined(val_true));
  REQUIRE(l2.undefined(val_undef));


  REQUIRE(l3.satisfied(val_false));
  REQUIRE(!l3.satisfied(val_true));
  REQUIRE(!l3.satisfied(val_undef));

  REQUIRE(!l3.falsified(val_false));
  REQUIRE(l3.falsified(val_true));
  REQUIRE(!l3.falsified(val_undef));

  REQUIRE(!l3.undefined(val_false));
  REQUIRE(!l3.undefined(val_true));
  REQUIRE(l3.undefined(val_undef));


  REQUIRE(!l4.satisfied(val_false));
  REQUIRE(l4.satisfied(val_true));
  REQUIRE(!l4.satisfied(val_undef));

  REQUIRE(l4.falsified(val_false));
  REQUIRE(!l4.falsified(val_true));
  REQUIRE(!l4.falsified(val_undef));

  REQUIRE(!l4.undefined(val_false));
  REQUIRE(!l4.undefined(val_true));
  REQUIRE(l4.undefined(val_undef));
}
