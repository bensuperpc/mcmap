#include "../src/colors.h"
#include <gtest/gtest.h>

Colors::Color water = "#0743c832";
Colors::Color dummy = "#ffffff";

TEST(TestColor, TestCreate) {
  Colors::Color c;

  ASSERT_TRUE(c.empty());

  ASSERT_FALSE(water.empty());
  ASSERT_EQ(water.R, 7);
  ASSERT_EQ(water.G, 67);
  ASSERT_EQ(water.B, 200);
  ASSERT_EQ(water.ALPHA, 50);
}

TEST(TestColor, TestEmpty) {
  Colors::Color c;
  c.R = c.G = c.B = c.ALPHA;
  ASSERT_TRUE(c.empty());

  Colors::Color cR = c, cG = c, cB = c;
  cR.R = 1;
  cG.G = 1;
  cB.B = 1;

  ASSERT_FALSE(cR.empty());
  ASSERT_FALSE(cG.empty());
  ASSERT_FALSE(cB.empty());
}

TEST(TestColor, TestOpacity) {
  Colors::Color c;
  ASSERT_TRUE(c.transparent());
  ASSERT_FALSE(c.opaque());

  c.ALPHA = 1;
  ASSERT_FALSE(c.transparent());
  ASSERT_FALSE(c.opaque());

  c.ALPHA = 255;
  ASSERT_FALSE(c.transparent());
  ASSERT_TRUE(c.opaque());
}

TEST(TestColor, TestJson) {
  Colors::Color b = water, translated;
  translated = nlohmann::json(b).get<Colors::Color>();

  ASSERT_EQ(b, translated);
}

TEST(TestBlock, TestCreateDefault) {
  Colors::Block b;

  ASSERT_TRUE(b.type == Colors::FULL);
  ASSERT_TRUE(b.primary.empty());
  ASSERT_TRUE(b.secondary.empty());
}

TEST(TestBlock, TestCreateType) {
  Colors::Block b = Colors::Block(Colors::drawSlab, dummy);

  ASSERT_TRUE(b.type == Colors::drawSlab);
  ASSERT_FALSE(b.primary.empty());
  ASSERT_TRUE(b.secondary.empty());
}

TEST(TestBlock, TestCreateTypeAccent) {
  Colors::Block b = Colors::Block(Colors::drawStair, dummy, dummy);

  ASSERT_TRUE(b.type == Colors::drawStair);
  ASSERT_FALSE(b.primary.empty());
  ASSERT_FALSE(b.secondary.empty());
}

TEST(TestBlock, TestEqualOperator) {
  Colors::Block b1 = Colors::Block(Colors::drawBeam, dummy), b2 = b1;

  ASSERT_EQ(b1, b2);
  b1.type = Colors::drawTransparent;
  ASSERT_NE(b1, b2);

  b2 = Colors::Block(Colors::drawTransparent, dummy, dummy);
  ASSERT_NE(b1, b2);
}

TEST(TestBlock, TestJson) {
  Colors::Block b, translated;
  b = Colors::Block(Colors::drawStair, dummy, dummy);
  translated = nlohmann::json(b).get<Colors::Block>();

  ASSERT_EQ(b, translated);
}

TEST(TestPalette, TestJson) {
  Colors::Palette p, translated;
  p.insert(std::pair("minecraft:water", Colors::Block(Colors::FULL, water)));
  translated = nlohmann::json(p).get<Colors::Palette>();

  ASSERT_EQ(p, translated);
}

TEST(TestColorImport, TestLoadEmbedded) {
  Colors::Palette p;

  ASSERT_TRUE(Colors::load(&p));
  ASSERT_TRUE(p.size());
}

TEST(TestColorImport, TestLoadFile) {
  Colors::Palette p;

  ASSERT_TRUE(Colors::load(&p, fs::path("tests/nowater.json")));
  ASSERT_TRUE(p.size());
  ASSERT_TRUE(p.find("minecraft:water") != p.end());
  ASSERT_TRUE(p["minecraft:water"].primary.transparent());
}

TEST(TestColorImport, TestLoadNoFile) {
  Colors::Palette p;

  ASSERT_FALSE(Colors::load(&p, fs::path("/non-existent")));
  ASSERT_FALSE(p.size());
}

TEST(TestColorImport, TestLoadBadFormat) {
  Colors::Palette p;

  ASSERT_FALSE(Colors::load(&p, fs::path("tests/bad.json")));
  ASSERT_FALSE(p.size());
}
