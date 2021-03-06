/* $Id$
 * $Name$
 * $ProjectName$
 */

/**
 * @file xml_parser.c
 *
 * Parse XML data.
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "feed_item.h"
#include "output.h"
#include "utils.h"
#include "web.h"


/** \cond */

struct rssNode {
	char *url;
	char *type;
};

typedef struct rssNode rssNode;

/** \endcond */

static void freeNode(rssNode *rss) {
	if (rss) {
		am_free(rss->url);
		rss->url = NULL;
		am_free(rss->type);
		rss->type = NULL;
	}
	am_free(rss);
	rss = NULL;
}

static int getNodeText(xmlNodePtr child, char **dest) {
	xmlChar * textNode;
	int result = 0;

   assert(dest && *dest == NULL);
	textNode = xmlNodeGetContent(child);
	*dest = am_strdup((char*) textNode);
	xmlFree(textNode);
	if (*dest) {
		result = 1;
  }
	return result;
}

static rssNode* getNodeAttributes(xmlNodePtr child) {
	rssNode *tmp = am_malloc(sizeof(rssNode));
	xmlAttrPtr attr = child->properties;

   tmp->url = NULL;
   tmp->type = NULL;

   while (attr) {
		if ((strcmp((char*) attr->name, "url") == 0)) {
			getNodeText(attr->children, &tmp->url);
		} else if ((strcmp((char*) attr->name, "content") == 0) || (strcmp(
				(char*) attr->name, "type") == 0)) {
			getNodeText(attr->children, &tmp->type);
		}
		attr = attr->next;
	}

	return tmp;
}

static simple_list extract_feed_items(xmlNodeSetPtr nodes) {
	xmlNodePtr cur = NULL, child = NULL;
	uint32_t size, i;
	feed_item item = NULL;
	uint8_t name_set, url_set;
	rssNode *enclosure = NULL;
	simple_list itemList = NULL;

	size = (nodes) ? nodes->nodeNr : 0;

	dbg_printf(P_INFO2, "%d items in XML", size);
	for (i = 0; i < size; ++i) {
		assert(nodes->nodeTab[i]);
		if (nodes->nodeTab[i]->type == XML_ELEMENT_NODE) {
			cur = nodes->nodeTab[i];
			if (cur->children) {
				child = cur->children;
				url_set = 0;
				name_set = 0;
				item = newFeedItem();

				while (child) {
					if ((strcmp((char*) child->name, "title") == 0)) {
						name_set = getNodeText(child->children, &item->name);
               } else if((strcmp((char*)child->name, "link") == 0)) {
                  char* link = NULL;
                  if(getNodeText(child->children, &link)) {
                     addItem(link, &item->urls);
                  }
               } else if ((strcmp((char*) child->name, "enclosure") == 0)) {
                  enclosure = getNodeAttributes(child);

                  if ( enclosure->url != NULL && enclosure->type != NULL && strncmp(enclosure->type, "video/", 6) == 0 ) {
                     addItem(am_strdup(enclosure->url), &item->urls);
   			      }

                  freeNode(enclosure);
			      }

			      child = child->next;
		      }

   	      if (name_set && listCount(item->urls) > 0) {
	   	      addItem(item, &itemList);
		      } else {
               freeFeedItem(item);
            }

		      child = cur = NULL;
	      }
      } else {
	      cur = nodes->nodeTab[i];
	      dbg_printf(P_ERROR, "Unknown node \"%s\": type %d", cur->name, cur->type);
      }
	}

	dbg_printf(P_INFO2, "== Done extracting RSS items ==");
	return itemList;
}

/** \brief Walk through given XML data and extract specific items
 *
 * \param data The XML data
 * \param size Size of the XML data
 * \param item_count number of found RSS nodes in the XML data
 * \param ttl Time-To-Live value for the specific feed
 * \return A list of RSS items.
 *
 * The function currently parses RSS-formatted XML data only.
 * It extracts the "//item" nodes of a RSS "//channel".
 * The items are then packaged into neat little rss items and returned as a list.
 */
simple_list parse_xmldata(const char* data, uint32_t size, uint32_t* item_count, uint32_t *ttl) {
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr xpathCtx = NULL;
	xmlXPathObjectPtr xpathObj = NULL;
	xmlNodeSetPtr ttlNode = NULL;
	simple_list rss_items = NULL;


	const xmlChar* ttlExpr = (xmlChar*) "//channel/ttl";
	const xmlChar* itemExpr = (xmlChar*) "//item";
	LIBXML_TEST_VERSION

	/* Init libxml */
	xmlInitParser();

	*item_count = 0;

	if(!data) {
		return NULL;
	}

	/* Load XML document */
	if((doc = xmlParseMemory(data, size)) == NULL) {
    doc = xmlRecoverMemory(data, size);
  }
  
	if (doc == NULL) {
		dbg_printf(P_ERROR, "Error: Unable to parse input data!");
		return NULL;
	}

	/* Create XPath evaluation context */
	xpathCtx = xmlXPathNewContext(doc);
	if (xpathCtx == NULL) {
		dbg_printf(P_ERROR, "Error: Unable to create new XPath context");
		xmlFreeDoc(doc);
		xmlCleanupParser();
		return NULL;
	}

	/* check for time-to-live element in RSS feed */
	if (ttl == 0) {
		xpathObj = xmlXPathEvalExpression(ttlExpr, xpathCtx);
		if (xpathObj != NULL) {
			ttlNode = xpathObj->nodesetval;

			if (ttlNode->nodeNr == 1 && ttlNode->nodeTab[0]->type == XML_ELEMENT_NODE) {
				*ttl = atoi((char*) xmlNodeGetContent(ttlNode->nodeTab[0]->children));
			}
			xmlXPathFreeObject(xpathObj);
		} else {
			dbg_printf(P_ERROR, "Error: Unable to evaluate TTL XPath expression");
		}
	}

	/* Extract RSS "items" from feed */
	xpathObj = xmlXPathEvalExpression(itemExpr, xpathCtx);
	if (xpathObj == NULL) {
		dbg_printf(P_ERROR, "Error: Unable to evaluate XPath expression \"%s\"",
				itemExpr);
		xmlXPathFreeContext(xpathCtx);
		xmlFreeDoc(doc);
		xmlCleanupParser();
		return NULL;
	}

	*item_count = xpathObj->nodesetval ? xpathObj->nodesetval->nodeNr : 0;
	rss_items = extract_feed_items(xpathObj->nodesetval);

	/* Cleanup */
	xmlXPathFreeObject(xpathObj);
	xmlXPathFreeContext(xpathCtx);
	xmlFreeDoc(doc);
	/* Shutdown libxml */
	xmlCleanupParser();
	return rss_items;
}

