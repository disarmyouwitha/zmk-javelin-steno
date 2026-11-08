#include "engine.h"
#include "button_script_manager.h"
#include "dictionary/dictionary.h"
#include "key.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

class Fixture final : public StenoDictionary {
public:
  Fixture() : StenoDictionary(2) {}
  const char *GetName() const override { return "repeat fixture"; }
  StenoDictionaryLookupResult Lookup(const StenoDictionaryLookup &q) const override {
    const char *text=nullptr;
    if (q.length==2 && q.strokes[0]==StenoStroke("HE") && q.strokes[1]==StenoStroke("HROE")) text="hello";
    if (q.length==2 && q.strokes[0]==StenoStroke("KAT") && q.strokes[1]==StenoStroke("TP")) text="red cat";
    if (q.length==1) {
      if(q.strokes[0]==StenoStroke("KAT"))text="cat";
      if(q.strokes[0]==StenoStroke("#"))text="=repeat_last_translation";
      if(q.strokes[0]==StenoStroke("HROE"))text="alone";
      if(q.strokes[0]==StenoStroke("S"))text="{&a}";
      if(q.strokes[0]==StenoStroke("T"))text="{&b}";
      if(q.strokes[0]==StenoStroke("-S"))text="{^s}";
      if(q.strokes[0]==StenoStroke("-F"))text="{.}";
    }
    return text ? StenoDictionaryLookupResult::CreateStaticString(text) : StenoDictionaryLookupResult::CreateInvalid();
  }
};
std::string output() {
  std::string result;uint32_t mods=0;
  for(const auto &event:Key::history) {
    auto code=event.code.value;
    if(code>=KeyCode::L_CTRL && code<=KeyCode::R_META) {
      if(event.isPress)mods|=1u<<(8+code-KeyCode::L_CTRL);else mods&=~(1u<<(8+code-KeyCode::L_CTRL));
      continue;
    }
    if(!event.isPress)continue;
    if(code==KeyCode::BACKSPACE){if(!result.empty())result.pop_back();continue;}
    auto c=KeyCode::ConvertToUnicode(code|mods);
    if(c && c<128)result+=char(c);
  }
  return result;
}
void check(const char *name,std::vector<const char*> sequence,const char *expected) {
 Fixture dictionary;StenoCompiledOrthography orthography(StenoOrthography::emptyOrthography);
 StenoEngine engine(dictionary,nullptr,orthography);Key::history.clear();
 for(auto s:sequence){StenoStroke stroke;stroke.Set(s);engine.Process(stroke);}
 auto actual=output();
 if(actual!=expected){fprintf(stderr,"FAIL %s: expected [%s], got [%s]\n",name,expected,actual.c_str());exit(1);}
 printf("PASS %s: [%s]\n",name,actual.c_str());
}
int main(){
 alignas(4) static const uint8_t script[64]={0x4a,0x53,0x53,0x34};ButtonScriptManager::Initialize(script);
 check("empty history",{"#"},"");
 check("single stroke",{"KAT","#"},"cat cat");
 check("repeat twice and undo",{"KAT","#","#","*"},"cat cat");
 check("multi-stroke translation",{"HE","HROE","#"},"hello hello");
 check("multi-stroke undo",{"HE","HROE","#","*"},"hello");
 check("multi-word translation",{"KAT","TP","#"},"red cat red cat");
 check("suffix formatting",{"KAT","-S","#"},"catss");
 check("glue formatting",{"S","T","#"},"abb");
 check("punctuation",{"KAT","-F","#"},"cat..");
 std::vector<const char*> longRepeat={"KAT"};std::string longExpected="cat";
 for(int i=0;i<40;i++){longRepeat.push_back("#");longExpected+=" cat";}
 check("long repeat history",longRepeat,longExpected.c_str());
}
