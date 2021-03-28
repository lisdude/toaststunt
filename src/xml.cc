/*
 * XML for the MOO Server using the expat library
 */



#include "bf_register.h"
#include "functions.h"
#include "db_tune.h"
#include "storage.h"
#include "list.h"
#include "streams.h"
#include "utils.h"

//#include "exceptions.h"
#include "tasks.h"

#include "expat.h"

/*
 * quick'n'dirty
 * <foo a="1">
 *   <bar>11</bar>
 * </foo> 
 * =
 * {"foo", {{"a", "1"}}, {{"bar", {}, {"11"}}}}
 */

typedef struct XMLdata XMLdata;

struct XMLdata {
  XMLdata *parent;
  Stream *body;
  Var element;
};

static XMLdata *
new_node(XMLdata *parent, const char *name) 
{
  /* TODO: may want a suballocator here; gonna be needing a lot of
   * these 2-ptr + 1 var nodes
   */
  XMLdata *node;
  Var element;
  node = (XMLdata *)mymalloc(1*sizeof(XMLdata), M_XML_DATA);
  element = new_list(4);
  /* {name, attribs, body, children} */ 
  element.v.list[1].type = TYPE_STR;
  element.v.list[1].v.str = str_dup(name);
  element.v.list[2] = new_list(0);
  element.v.list[3].type = TYPE_INT;
  element.v.list[3].v.num = 0;
  element.v.list[4] = new_list(0);
  node->body = NULL;
  node->element = element;
  node->parent = parent;
  return node;
}			

static void 
finish_node(XMLdata *data)
{
  XMLdata *parent = data->parent;
  Var element = data->element;
  Var body;
  Stream *s = data->body;
  body.type = TYPE_STR;
  if(s == NULL) {
    body.v.str = str_dup("");
  } else {
    body.v.str = str_dup(reset_stream(s));
  }
  element.v.list[3] = body;
  if(parent != NULL) {
    Var pelement = parent->element;
    pelement.v.list[4] = listappend(pelement.v.list[4], var_ref(element));
  }
}
  
static void
free_node(XMLdata *data) 
{
  free_var(data->element);
  if(data->body != NULL)
    free_stream(data->body);
  myfree(data, M_XML_DATA);
}

static void
flush_nodes(XMLdata *bottom) 
{
  XMLdata *parent = bottom->parent;
  free_node(bottom);
  if(parent != NULL) {
    flush_nodes(parent);
  }
}

static void
xml_startElement(void *userData, const char *name, const char **atts)
{
  XMLdata **data = (XMLdata**)userData;
  XMLdata *parent = *data;

  XMLdata *node = new_node(parent, name);
  const char **patts = atts;

  while(*patts != NULL) {
    Var pair = new_list(2);
    pair.v.list[1].type = TYPE_STR;
    pair.v.list[1].v.str = str_dup(patts[0]);
    pair.v.list[2].type = TYPE_STR;
    pair.v.list[2].v.str = str_dup(patts[1]); 
    patts += 2;
    node->element.v.list[2] = listappend(node->element.v.list[2], pair);
  }
  *data = node;
}

static void 
xml_characterDataHandler(void *userData, const XML_Char *s, int len)
{
  XMLdata **data = (XMLdata**)userData;
  XMLdata *node = *data;
  Stream *sp = node->body;

  if(sp == NULL) {
    node->body = new_stream(len);
    sp = node->body; 
  }

  stream_add_string(sp, raw_bytes_to_binary(s, len));
}

static void 
xml_streamCharacterDataHandler(void *userData, const XML_Char *s, int len)
{
  XMLdata **data = (XMLdata**)userData;
  XMLdata *node = *data;
  Var element = node->element;
  Var v;
  v.type = TYPE_STR;
  v.v.str = str_dup(raw_bytes_to_binary(s, len));
  element.v.list[4] = listappend(element.v.list[4], v);
}


static void
xml_endElement(void *userData, const char *name)
{
  XMLdata **data = (XMLdata**)userData;
  XMLdata *node = *data;
  XMLdata *parent = node->parent;
  finish_node(node);
  free_node(node);
  *data = parent;
}

/**
 * Parse an XML string into a nested list.
 * The second parameter indicates if body text (text within XML tags)
 * should show up among the children of the tag or in its own
 * section.
 *
 * See documentation (ext-xml.README) for examples.
 */
static package 
parse_xml(const char *data, int bool_stream)
  {
  /*
   * FIXME: Feed expat smaller chunks of the string and 
   * check for task timeout between chunks
   *
   */
  int decoded_length;
  const char *decoded;
  package result; 
  XML_Parser parser = XML_ParserCreate(NULL);
  XMLdata *root = new_node(NULL, "");
  XMLdata *child = root;
  
  decoded_length = strlen(data);
  decoded = data;
  XML_SetUserData(parser, &child);
  XML_SetElementHandler(parser, xml_startElement, xml_endElement);
  if(bool_stream) {
    XML_SetCharacterDataHandler(parser, xml_streamCharacterDataHandler);
  } else {
    XML_SetCharacterDataHandler(parser, xml_characterDataHandler);
  }
  if (!XML_Parse(parser, decoded, decoded_length, 1)) {
    Var r;
    r.type = TYPE_INT;
    r.v.num = XML_GetCurrentByteIndex(parser);
    flush_nodes(child);
    result = make_raise_pack(E_INVARG, 
			     XML_ErrorString(XML_GetErrorCode(parser)),
			     r);
  } else {
    finish_node(root);
    result = make_var_pack(var_ref(root->element.v.list[4].v.list[1]));
    free_node(root);
  }
  XML_ParserFree(parser);
  return result; 
}


static package
bf_parse_xml_document(Var arglist, Byte next, void *vdata, Objid progr) 
{
  package result = parse_xml(arglist.v.list[1].v.str, 1);
  free_var(arglist);
  return result;
}

static package
bf_parse_xml_tree(Var arglist, Byte next, void *vdata, Objid progr)
{
  package result = parse_xml(arglist.v.list[1].v.str, 0);
  free_var(arglist);
  return result;
}

void
register_xml()
{
    register_function("xml_parse_tree", 1, 1, bf_parse_xml_tree, TYPE_STR);
    register_function("xml_parse_document", 1, 1, bf_parse_xml_document, TYPE_STR);
}

char rcsid_xml[] = "$Id: ext-xml.c,v 1.1 2000/05/12 06:12:11 fox Exp $";