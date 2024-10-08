/*
 * Copyright 2012--2014 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors:
 *      Martin Preisler <mpreisle@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "public/scap_ds.h"
#include "public/oscap_text.h"
#include "public/oscap.h"

#include "common/_error.h"
#include "common/util.h"
#include "common/list.h"
#include "common/debug_priv.h"

#include "ds_common.h"
#include "ds_rds_session.h"
#include "ds_rds_session_priv.h"
#include "rds_priv.h"
#include "sds_priv.h"
#include "source/public/oscap_source.h"
#include "source/oscap_source_priv.h"

#include <sys/stat.h>
#include <time.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include "oscap_helpers.h"

static const char* arf_ns_uri = "http://scap.nist.gov/schema/asset-reporting-format/1.1";
static const char* core_ns_uri = "http://scap.nist.gov/schema/reporting-core/1.1";
static const char* arfvocab_ns_uri = "http://scap.nist.gov/specifications/arf/vocabulary/relationships/1.0#";
static const char* ai_ns_uri = "http://scap.nist.gov/schema/asset-identification/1.1";
static const char* xlink_ns_uri = "http://www.w3.org/1999/xlink";


xmlNode *ds_rds_lookup_container(xmlDocPtr doc, const char *container_name)
{
	xmlNodePtr root = xmlDocGetRootElement(doc);
	xmlNodePtr ret = NULL;

	if (root == NULL)
		return NULL;

	xmlNodePtr candidate = root->children;

	for (; candidate != NULL; candidate = candidate->next)
	{
		if (candidate->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char*)(candidate->name), container_name) == 0) {
			ret = candidate;
			break;
		}
	}

	return ret;
}

xmlNode *ds_rds_lookup_component(xmlDocPtr doc, const char *container_name, const char *component_name, const char *id)
{
	xmlNodePtr container = ds_rds_lookup_container(doc, container_name);
	xmlNodePtr component = NULL;

	if (container == NULL)
		return NULL;

	for (xmlNode *candidate = container->children; candidate != NULL; candidate = candidate->next) {
		if (candidate->type != XML_ELEMENT_NODE)
			continue;

		if (!oscap_streq((const char*)(candidate->name), component_name))
			continue;

		char* candidate_id = (char*)xmlGetProp(candidate, BAD_CAST "id");
		if (oscap_streq(candidate_id, id)) {
			component = candidate;
			xmlFree(candidate_id);
			break;
		}
		xmlFree(candidate_id);
	}
	return component;
}

static xmlNodePtr ds_rds_get_inner_content(xmlDocPtr doc, xmlNodePtr parent_node)
{
	xmlNodePtr candidate = parent_node->children;
	xmlNodePtr content_node = NULL;

	for (; candidate != NULL; candidate = candidate->next)
	{
		if (candidate->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char*)(candidate->name), "content") == 0) {
			content_node = candidate;
			break;
		}
	}

	if (!content_node) {
		oscap_seterr(OSCAP_EFAMILY_XML, "Given ARF node has no 'arf:content' node inside!");
		return NULL;
	}

	return content_node;
}

int ds_rds_dump_arf_content(struct ds_rds_session *session, const char *container_name, const char *component_name, const char *content_id)
{
	xmlDoc *doc = ds_rds_session_get_xmlDoc(session);
	xmlNodePtr parent_node = ds_rds_lookup_component(doc, container_name, component_name, content_id);
	if (!parent_node) {
		oscap_seterr(OSCAP_EFAMILY_OSCAP, "Could not find any %s of id '%s'", component_name, content_id);
		return -1;
	}

	xmlNodePtr content_node = ds_rds_get_inner_content(NULL, parent_node);

	if (!content_node)
		return -1;

	xmlNodePtr candidate = content_node->children;
	xmlNodePtr inner_root = NULL;

	for (; candidate != NULL; candidate = candidate->next)
	{
		if (candidate->type != XML_ELEMENT_NODE)
			continue;

		if (inner_root) {
			oscap_seterr(OSCAP_EFAMILY_XML, "There are multiple nodes inside an 'arf:content' node. "
				"Only the last one will be used!");
		}

		inner_root = candidate;
	}
	if (inner_root == NULL) {
		oscap_seterr(OSCAP_EFAMILY_XML, "Could not found any child inside 'arf:content' node when looking for %s.",
				content_id);
		return -1;
	}

	// We assume that arf:content is XML. This is reasonable because both
	// reports and report requests are XML documents.
	xmlDoc *new_doc = ds_doc_from_foreign_node(inner_root, ds_rds_session_get_xmlDoc(session));
	char *target_file = oscap_sprintf("%s/%s.xml", ds_rds_session_get_target_dir(session), component_name);
	struct oscap_source *source = oscap_source_new_from_xmlDoc(new_doc, target_file);
	free(target_file);
	return ds_rds_session_register_component_source(session, content_id, source);
}

xmlNodePtr ds_rds_create_report(xmlDocPtr target_doc, xmlNodePtr reports_node, xmlDocPtr source_doc, const char* report_id)
{
	xmlNsPtr arf_ns = xmlSearchNsByHref(target_doc, xmlDocGetRootElement(target_doc), BAD_CAST arf_ns_uri);

	xmlNodePtr report = xmlNewNode(arf_ns, BAD_CAST "report");
	xmlSetProp(report, BAD_CAST "id", BAD_CAST report_id);

	xmlNodePtr report_content = xmlNewNode(arf_ns, BAD_CAST "content");
	xmlAddChild(report, report_content);

	xmlDOMWrapCtxtPtr wrap_ctxt = xmlDOMWrapNewCtxt();
	xmlNodePtr res_node = NULL;
	xmlDOMWrapCloneNode(wrap_ctxt, source_doc, xmlDocGetRootElement(source_doc),
			&res_node, target_doc, NULL, 1, 0);
	xmlAddChild(report_content, res_node);
	xmlDOMWrapReconcileNamespaces(wrap_ctxt, res_node, 0);
	xmlDOMWrapFreeCtxt(wrap_ctxt);

	xmlAddChild(reports_node, report);

	return report;
}

static void ds_rds_add_relationship(xmlDocPtr doc, xmlNodePtr relationships,
		const char* type, const char* subject, const char* ref)
{
	xmlNsPtr core_ns = xmlSearchNsByHref(doc, xmlDocGetRootElement(doc), BAD_CAST core_ns_uri);

	// create relationship between given request and the report
	xmlNodePtr relationship = xmlNewNode(core_ns, BAD_CAST "relationship");
	xmlSetProp(relationship, BAD_CAST "type", BAD_CAST type);
	xmlSetProp(relationship, BAD_CAST "subject", BAD_CAST subject);

	xmlNodePtr ref_node = xmlNewNode(core_ns, BAD_CAST "ref");
	xmlNodeSetContent(ref_node, BAD_CAST ref);
	xmlAddChild(relationship, ref_node);

	xmlAddChild(relationships, relationship);
}

static xmlNodePtr ds_rds_add_ai_from_xccdf_results(xmlDocPtr doc, xmlNodePtr assets,
		xmlDocPtr xccdf_result_doc)
{
	xmlNsPtr arf_ns = xmlSearchNsByHref(doc, xmlDocGetRootElement(doc), BAD_CAST arf_ns_uri);
	xmlNsPtr ai_ns = xmlSearchNsByHref(doc, xmlDocGetRootElement(doc), BAD_CAST ai_ns_uri);

	xmlNodePtr asset = xmlNewNode(arf_ns, BAD_CAST "asset");

	// Lets figure out a unique asset identification
	// The format is: "asset%i" where %i is a increasing integer suffix
	//
	// We use a very simple optimization, we know that assets will be "ordered"
	// by their @id because we are adding them there in that order.
	// Whenever we get a collision we can simply bump the suffix and continue,
	// no need to go back and check the previous assets.

	xmlNodePtr child_asset = assets->children;

	unsigned int suffix = 0;
	for (; child_asset != NULL; child_asset = child_asset->next)
	{
		if (child_asset->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char*)(child_asset->name), "asset") != 0)
			continue;

		char* id_candidate = oscap_sprintf("asset%i", suffix);
		xmlChar* id = xmlGetProp(child_asset, BAD_CAST "id");

		if (strcmp(id_candidate, (const char*)id) == 0)
		{
			suffix++;
		}
		xmlFree(id);
		free(id_candidate);
	}

	char* id = oscap_sprintf("asset%i", suffix);
	xmlSetProp(asset, BAD_CAST "id", BAD_CAST id);
	free(id);

	xmlAddChild(assets, asset);

	xmlNodePtr computing_device = xmlNewNode(ai_ns, BAD_CAST "computing-device");
	xmlAddChild(asset, computing_device);

	xmlNodePtr connections = xmlNewNode(ai_ns, BAD_CAST "connections");
	xmlAddChild(computing_device, connections);

	xmlNodePtr test_result = xmlDocGetRootElement(xccdf_result_doc);

	xmlNodePtr test_result_child = test_result->children;

	xmlNodePtr last_fqdn = NULL;
	xmlNodePtr last_hostname = NULL;
	for (; test_result_child != NULL; test_result_child = test_result_child->next)
	{
		if (test_result_child->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char*)(test_result_child->name), "target-address") == 0)
		{
			xmlNodePtr connection = xmlNewNode(ai_ns, BAD_CAST "connection");
			xmlAddChild(connections, connection);
			xmlNodePtr ip_address = xmlNewNode(ai_ns, BAD_CAST "ip-address");
			xmlAddChild(connection, ip_address);

			xmlChar* content = xmlNodeGetContent(test_result_child);

			// we need to figure out whether the address is IPv4 or IPv6
			if (strchr((char*)content, '.') != NULL) // IPv4 has to have 4 dots
			{
				xmlNewTextChild(ip_address, ai_ns, BAD_CAST "ip-v4", content);
			}
			else // IPv6 has semicolons instead of dots
			{
				// lets expand the IPv6 to conform to the AI XSD and specification
				char *expanded_ipv6 = oscap_expand_ipv6((const char*)content);
				xmlNewTextChild(ip_address, ai_ns, BAD_CAST "ip-v6", BAD_CAST expanded_ipv6);
				free(expanded_ipv6);
			}
			xmlFree(content);
		}
		else if (strcmp((const char*)(test_result_child->name), "target-facts") == 0)
		{
			xmlNodePtr target_fact_child = test_result_child->children;

			for (; target_fact_child != NULL; target_fact_child = target_fact_child->next)
			{
				if (target_fact_child->type != XML_ELEMENT_NODE)
					continue;

				if (strcmp((const char*)(target_fact_child->name), "fact") != 0)
					continue;

				xmlChar *name = xmlGetProp(target_fact_child, BAD_CAST "name");
				if (name) {
					if (!strcmp((const char*)name, "urn:xccdf:fact:asset:identifier:mac")) {
						xmlChar *content = xmlNodeGetContent(target_fact_child);
						xmlNodePtr connection = xmlNewNode(ai_ns, BAD_CAST "connection");
						xmlAddChild(connections, connection);
						xmlNewTextChild(connection, ai_ns, BAD_CAST "mac-address", content);
						xmlFree(content);
					}

					// Order for the output to be valid: fqdn then hostname, just one of each kind

					if (!strcmp((const char*)name, "urn:xccdf:fact:asset:identifier:fqdn")) {
						xmlChar *content = xmlNodeGetContent(target_fact_child);
						xmlNodePtr fqdn = xmlNewNode(ai_ns, BAD_CAST "fqdn");
						xmlNodeSetContent(fqdn, BAD_CAST content);
						if (!last_fqdn)
							last_fqdn = last_hostname ? xmlAddPrevSibling(last_hostname, fqdn) : xmlAddChild(computing_device, fqdn);
						xmlFree(content);
					}

					if (!strcmp((const char*)name, "urn:xccdf:fact:asset:identifier:host_name")) {
						xmlChar *content = xmlNodeGetContent(target_fact_child);
						xmlNodePtr hostname = xmlNewNode(ai_ns, BAD_CAST "hostname");
						xmlNodeSetContent(hostname, BAD_CAST content);
						if (!last_hostname)
							last_hostname = last_fqdn ? xmlAddNextSibling(last_fqdn, hostname) : xmlAddChild(computing_device, hostname);
						xmlFree(content);
					}
				}
				xmlFree(name);
			}
		}
	}

	if (xmlGetLastChild(connections) == NULL) {
		xmlUnlinkNode(connections);
		xmlFreeNode(connections);
	}

	return asset;
}

static int ds_rds_report_inject_ai_target_id_ref(xmlDocPtr doc, xmlNodePtr test_result_node, const char *asset_id)
{
	// Now we need to find the right place to inject the target-id-ref element.
	// It has to come after target, target-address and target-facts elements.
	// However target-address and target-fact are both optional.

	xmlNodePtr prev_sibling = NULL;
	xmlNodePtr prev_sibling_candidate = test_result_node->children;

	while (prev_sibling_candidate) {
		if (prev_sibling_candidate->type == XML_ELEMENT_NODE) {
			if (strcmp((const char*)prev_sibling_candidate->name, "target") == 0 ||
				strcmp((const char*)prev_sibling_candidate->name, "target-address") == 0 ||
				strcmp((const char*)prev_sibling_candidate->name, "target-facts") == 0) {

				prev_sibling = prev_sibling_candidate;
			}
		}

		prev_sibling_candidate = prev_sibling_candidate->next;
	}

	if (!prev_sibling) {
		oscap_seterr(OSCAP_EFAMILY_XML, "No target element was found in TestResult. "
			"The most likely reason is that the content is not valid! "
			"(XCCDF spec states 'target' element as required)");
		return -1;
	}

	// We have to make sure we are not injecting a target-id-ref that is there
	// already. if there is any duplicate, it has to come right after prev_sibling.
	xmlNodePtr duplicate_candidate = prev_sibling->next;
	while (duplicate_candidate) {
		if (duplicate_candidate->type == XML_ELEMENT_NODE) {
			if (strcmp((const char*)duplicate_candidate->name, "target-id-ref") == 0) {
				xmlChar* system_attr = xmlGetProp(duplicate_candidate, BAD_CAST "system");
				xmlChar* name_attr = xmlGetProp(duplicate_candidate, BAD_CAST "name");

				if (strcmp((const char*)system_attr, ai_ns_uri) == 0 &&
					strcmp((const char*)name_attr, asset_id) == 0) {

					xmlFree(system_attr);
					xmlFree(name_attr);
					return 0;
				}

				xmlFree(system_attr);
				xmlFree(name_attr);
			}
			else {
				break;
			}
		}
		duplicate_candidate = duplicate_candidate->next;
	}

	xmlNodePtr target_id_ref = xmlNewNode(prev_sibling->ns, BAD_CAST "target-id-ref");
	xmlNewProp(target_id_ref, BAD_CAST "system", BAD_CAST ai_ns_uri);
	xmlNewProp(target_id_ref, BAD_CAST "name", BAD_CAST asset_id);
	// @href is a required attribute by the XSD! The spec advocates filling it
	// blank when it's not needed.
	xmlNewProp(target_id_ref, BAD_CAST "href", BAD_CAST "");

	xmlAddNextSibling(prev_sibling, target_id_ref);

	return 0;
}

static void ds_rds_report_inject_check_content_ref(xmlNodePtr check_content_ref, struct oscap_htable *arf_report_mapping)
{
	if (check_content_ref->type == XML_ELEMENT_NODE) {
		if (strcmp((const char*)check_content_ref->name, "check-content-ref") == 0) {
			char *oval_filename = (char *) xmlGetProp(check_content_ref,
					BAD_CAST "href");
			if (oval_filename == NULL) {
				return;
			}
			char *report_id = oscap_htable_get(arf_report_mapping, oval_filename);
			if (report_id == NULL) {
				free(oval_filename);
				return;
			}
			char *desired_href = oscap_sprintf("#%s", report_id);
			xmlSetProp(check_content_ref, BAD_CAST "href", BAD_CAST desired_href);
			free(desired_href);
			free(oval_filename);
		}
	}
}

static void ds_rds_report_inject_rule_result_check_refs(xmlDocPtr doc, xmlNodePtr rule_result, struct oscap_htable *arf_report_mapping)
{
	xmlNodePtr child = rule_result->children;

	while (child) {
		if (child->type == XML_ELEMENT_NODE) {
			if (strcmp((const char*)child->name, "complex-check") == 0) {
				ds_rds_report_inject_rule_result_check_refs(doc, child, arf_report_mapping);
			} else if (strcmp((const char*)child->name, "check") == 0) {
				xmlNodePtr check_content_ref = child->children;

				while (check_content_ref) {
					ds_rds_report_inject_check_content_ref(check_content_ref, arf_report_mapping);
					check_content_ref = check_content_ref->next;
				}
			}
		}

		child = child->next;
	}
}

static void ds_rds_report_inject_rule_result_refs(xmlDocPtr doc, xmlNodePtr test_result_node, struct oscap_htable *arf_report_mapping)
{
	xmlNodePtr child = test_result_node->children;
	while (child) {
		if (child->type == XML_ELEMENT_NODE) {
			if (strcmp((const char*)child->name, "rule-result") == 0) {
				ds_rds_report_inject_rule_result_check_refs(doc, child, arf_report_mapping);
			}
		}

		child = child->next;
	}
}

static int ds_rds_report_inject_refs(xmlDocPtr doc, xmlNodePtr report, const char *asset_id, struct oscap_htable* arf_report_mapping)
{
	xmlNodePtr content_node = ds_rds_get_inner_content(doc, report);

	if (!content_node)
		return -1;

	if (!content_node->children) {
		oscap_seterr(OSCAP_EFAMILY_XML, "Given report doesn't contain any data, "
			"can't inject AI asset target id ref");
		return -1;
	}

	xmlNodePtr test_result_node = NULL;
	xmlNodePtr test_result_candidate = content_node->children;
	xmlNodePtr inner_element_node = NULL;

	while (test_result_candidate) {
		if (test_result_candidate->type == XML_ELEMENT_NODE) {
			inner_element_node = test_result_candidate;

			if (strcmp((const char*)test_result_candidate->name, "TestResult") == 0) {
				test_result_node = test_result_candidate;
				break;
			}
		}

		test_result_candidate = test_result_candidate->next;
	}

	if (!inner_element_node) {
		oscap_seterr(OSCAP_EFAMILY_XML, "Given report doesn't contain any XML element! "
			"Can't inject AI asset target id ref");
		return -1;
	}

	if (!test_result_node) {
		// TestResult may not be the top level element in the report.
		// While that is very unusual it is legitimate, lets check child elements.

		// As a rule, we only inject target-id-ref to the last test result
		// (XML, top-down).

		if (strcmp((const char*)inner_element_node->name, "Benchmark")) {
			oscap_seterr(OSCAP_EFAMILY_XML, "Top level element of the report isn't TestResult "
				"or Benchmark, the report is likely invalid!");
			return -1;
		}

		if (!inner_element_node->children) {
			oscap_seterr(OSCAP_EFAMILY_XML, "Top level element of the report isn't TestResult "
				"and does not contain any children! No TestResult to inject to has been found.");
			return -1;
		}

		test_result_candidate = inner_element_node->children;
		while (test_result_candidate) {
			if (test_result_candidate->type == XML_ELEMENT_NODE) {
				if (strcmp((const char*)test_result_candidate->name, "TestResult") == 0) {
					test_result_node = test_result_candidate;
					// we intentionally do not break here, we are looking for the
					// last (top-down) TestResult in the report.
					//break;
				}
			}

			test_result_candidate = test_result_candidate->next;
		}
	}

	if (!test_result_node) {
		oscap_seterr(OSCAP_EFAMILY_XML, "TestResult node to inject to has not been found"
			"(checked root element and all children of it).");
		return -1;
	}

	int ret = ds_rds_report_inject_ai_target_id_ref(doc, test_result_node, asset_id);

	ds_rds_report_inject_rule_result_refs(doc, test_result_node, arf_report_mapping);

	return ret;
}

static void ds_rds_add_xccdf_test_results(xmlDocPtr doc, xmlNodePtr reports,
		xmlDocPtr xccdf_result_file_doc, xmlNodePtr relationships, xmlNodePtr assets,
		const char* report_request_id, struct oscap_htable *arf_report_mapping)
{
	xmlNodePtr root_element = xmlDocGetRootElement(xccdf_result_file_doc);

	if (root_element->ns && root_element->ns->href &&
			oscap_str_endswith((const char*)root_element->ns->href, "xccdf/1.1")) {
		dW("Exporting ARF from XCCDF 1.1 is not allowed by SCAP specification. "
		   "The resulting ARF will not validate. Convert the input to XCCDF 1.2 "
		   "to get valid ARF results. The xccdf_1.1_to_1.2.xsl transformation."
		   "that ships with OpenSCAP can do that automatically.");
	}

	// There are 2 possible scenarios here:

	// 1) root element of given xccdf result file doc is a TestResult element
	// This is the easier scenario, we will just use ds_rds_create_report and
	// be done with it.
	if (strcmp((const char*)root_element->name, "TestResult") == 0)
	{
		xmlNodePtr report = ds_rds_create_report(doc, reports, xccdf_result_file_doc, "xccdf1");
		ds_rds_add_relationship(doc, relationships, "arfvocab:createdFor",
				"xccdf1", report_request_id);

		xmlNodePtr asset = ds_rds_add_ai_from_xccdf_results(doc, assets, xccdf_result_file_doc);
		char* asset_id = (char*)xmlGetProp(asset, BAD_CAST "id");
		ds_rds_add_relationship(doc, relationships, "arfvocab:isAbout",
				"xccdf1", asset_id);

		// We deliberately don't act on errors in inject refs as
		// these aren't fatal errors.
		ds_rds_report_inject_refs(doc, report, asset_id, arf_report_mapping);

		xmlFree(asset_id);
	}

	// 2) the root element is a Benchmark, TestResults are embedded within
	// We will have to walk through all elements, wrap each TestResult
	// in a xmlDoc and add them separately
	else if (strcmp((const char*)root_element->name, "Benchmark") == 0)
	{
		unsigned int report_suffix = 1;

		xmlNodePtr candidate_result = root_element->children;

		for (; candidate_result != NULL; candidate_result = candidate_result->next)
		{
			if (candidate_result->type != XML_ELEMENT_NODE)
				continue;

			if (strcmp((const char*)(candidate_result->name), "TestResult") != 0)
				continue;

			xmlDocPtr wrap_doc = xmlNewDoc(BAD_CAST "1.0");

			xmlDOMWrapCtxtPtr wrap_ctxt = xmlDOMWrapNewCtxt();
			xmlNodePtr res_node = NULL;
			xmlDOMWrapCloneNode(wrap_ctxt, xccdf_result_file_doc, candidate_result,
					&res_node, wrap_doc, NULL, 1, 0);
			xmlDocSetRootElement(wrap_doc, res_node);
			xmlDOMWrapReconcileNamespaces(wrap_ctxt, res_node, 0);
			xmlDOMWrapFreeCtxt(wrap_ctxt);

			char* report_id = oscap_sprintf("xccdf%i", report_suffix++);
			xmlNodePtr report = ds_rds_create_report(doc, reports, wrap_doc, report_id);
			ds_rds_add_relationship(doc, relationships, "arfvocab:createdFor",
					report_id, report_request_id);

			xmlNodePtr asset = ds_rds_add_ai_from_xccdf_results(doc, assets, wrap_doc);
			char* asset_id = (char*)xmlGetProp(asset, BAD_CAST "id");
			ds_rds_add_relationship(doc, relationships, "arfvocab:isAbout",
					report_id, asset_id);

			// We deliberately don't act on errors in inject ref as
			// these aren't fatal errors.
			ds_rds_report_inject_refs(doc, report, asset_id, arf_report_mapping);

			xmlFree(asset_id);

			free(report_id);

			xmlFreeDoc(wrap_doc);
		}
	}

	else
	{
		char* error = oscap_sprintf(
				"Unknown root element '%s' in given XCCDF result document, expected TestResult or Benchmark.",
				(const char*)root_element->name);

		oscap_seterr(OSCAP_EFAMILY_XML, "%s", error);
		free(error);
	}
}

static int _ds_rds_create_from_dom(xmlDocPtr *ret, xmlDocPtr sds_doc,
		xmlDocPtr tailoring_doc, const char *tailoring_filepath,
		char *tailoring_doc_timestamp, xmlDocPtr xccdf_result_file_doc,
		struct oscap_htable *oval_result_sources,
		struct oscap_htable *oval_result_mapping,
		struct oscap_htable *arf_report_mapping,
		bool clone)
{
	*ret = NULL;

	xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
	xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "asset-report-collection");
	xmlDocSetRootElement(doc, root);

	xmlNsPtr arf_ns = xmlNewNs(root, BAD_CAST arf_ns_uri, BAD_CAST "arf");
	xmlSetNs(root, arf_ns);

	xmlNsPtr core_ns = xmlNewNs(root, BAD_CAST core_ns_uri, BAD_CAST "core");
	xmlNewNs(root, BAD_CAST ai_ns_uri, BAD_CAST "ai");

	xmlNodePtr relationships = xmlNewNode(core_ns, BAD_CAST "relationships");
	xmlNewNs(relationships, BAD_CAST arfvocab_ns_uri, BAD_CAST "arfvocab");
	xmlAddChild(root, relationships);

	xmlNodePtr report_requests = xmlNewNode(arf_ns, BAD_CAST "report-requests");
	xmlAddChild(root, report_requests);

	xmlNodePtr assets = xmlNewNode(arf_ns, BAD_CAST "assets");
	xmlAddChild(root, assets);

	xmlNodePtr report_request = xmlNewNode(arf_ns, BAD_CAST "report-request");
	xmlSetProp(report_request, BAD_CAST "id", BAD_CAST "collection1");

	xmlNodePtr arf_content = xmlNewNode(arf_ns, BAD_CAST "content");

	xmlDOMWrapCtxtPtr sds_wrap_ctxt = xmlDOMWrapNewCtxt();
	xmlNodePtr sds_res_node = NULL;
	if (clone) {
		xmlDOMWrapCloneNode(sds_wrap_ctxt, sds_doc, xmlDocGetRootElement(sds_doc),
				&sds_res_node, doc, NULL, 1, 0);
	} else {
		sds_res_node = xmlDocGetRootElement(sds_doc);
		xmlDOMWrapAdoptNode(sds_wrap_ctxt, sds_doc, sds_res_node, doc, NULL, 0);
	}
	xmlAddChild(arf_content, sds_res_node);
	xmlDOMWrapReconcileNamespaces(sds_wrap_ctxt, sds_res_node, 0);
	xmlDOMWrapFreeCtxt(sds_wrap_ctxt);

	if (tailoring_doc && strcmp(tailoring_filepath, "NONEXISTENT")) {
		char *mangled_tailoring_filepath = ds_sds_mangle_filepath(tailoring_filepath);
		char *tailoring_component_id = oscap_sprintf("scap_org.open-scap_comp_%s_tailoring", mangled_tailoring_filepath);
		char *tailoring_component_ref_id = oscap_sprintf("scap_org.open-scap_cref_%s_tailoring", mangled_tailoring_filepath);

		// Need unique id (ref_id) - if generated already exists, then create new one
		int counter = 0;
		while (lookup_component_in_collection(sds_res_node, tailoring_component_id) != NULL) {
			free(tailoring_component_id);
			tailoring_component_id = oscap_sprintf("scap_org.open-scap_comp_%s_tailoring%03d", mangled_tailoring_filepath, counter++);
		}

		counter = 0;
		while (ds_sds_find_component_ref(xmlDocGetRootElement((xmlDocPtr) sds_res_node)->children, tailoring_component_ref_id) != NULL) {
			free(tailoring_component_ref_id);
			tailoring_component_ref_id = oscap_sprintf("scap_org.open-scap_cref_%s_tailoring%03d", mangled_tailoring_filepath, counter++);
		}

		free(mangled_tailoring_filepath);

		xmlDOMWrapCtxtPtr tailoring_wrap_ctxt = xmlDOMWrapNewCtxt();
		xmlNodePtr tailoring_res_node = NULL;
		xmlDOMWrapCloneNode(tailoring_wrap_ctxt, tailoring_doc, xmlDocGetRootElement(tailoring_doc),
				&tailoring_res_node, doc, NULL, 1, 0);
		xmlNsPtr sds_ns = sds_res_node->ns;
		xmlNodePtr tailoring_component = xmlNewNode(sds_ns, BAD_CAST "component");
		xmlSetProp(tailoring_component, BAD_CAST "id", BAD_CAST tailoring_component_id);
		xmlSetProp(tailoring_component, BAD_CAST "timestamp", BAD_CAST tailoring_doc_timestamp);
		xmlAddChild(tailoring_component, tailoring_res_node);
		xmlAddChild(sds_res_node, tailoring_component);

		xmlNodePtr checklists_element = NULL;
		xmlNodePtr datastream_element = node_get_child_element(sds_res_node, "data-stream");
		if (datastream_element == NULL) {
			datastream_element = xmlNewNode(sds_ns, BAD_CAST "data-stream");
			xmlAddChild(sds_res_node, datastream_element);
			checklists_element = xmlNewNode(sds_ns, BAD_CAST "checklists");
			xmlAddChild(datastream_element, checklists_element);
		}
		else {
			checklists_element = node_get_child_element(datastream_element, "checklists");
		}

		xmlNodePtr tailoring_component_ref = xmlNewNode(sds_ns, BAD_CAST "component-ref");
		xmlSetProp(tailoring_component_ref, BAD_CAST "id", BAD_CAST tailoring_component_ref_id);
		free(tailoring_component_ref_id);
		xmlNsPtr xlink_ns = xmlSearchNsByHref(doc, sds_res_node, BAD_CAST xlink_ns_uri);
		if (!xlink_ns) {
			xlink_ns = xmlNewNs(tailoring_component_ref, BAD_CAST xlink_ns_uri, BAD_CAST "xlink");
		}
		char *tailoring_cref_href = oscap_sprintf("#%s", tailoring_component_id);
		free(tailoring_component_id);
		xmlSetNsProp(tailoring_component_ref, xlink_ns, BAD_CAST "href", BAD_CAST tailoring_cref_href);
		free(tailoring_cref_href);
		xmlAddChild(checklists_element, tailoring_component_ref);

		xmlDOMWrapReconcileNamespaces(tailoring_wrap_ctxt, tailoring_res_node, 0);
		xmlDOMWrapFreeCtxt(tailoring_wrap_ctxt);
	}

	xmlAddChild(report_request, arf_content);

	xmlAddChild(report_requests, report_request);

	xmlNodePtr reports = xmlNewNode(arf_ns, BAD_CAST "reports");

	ds_rds_add_xccdf_test_results(doc, reports, xccdf_result_file_doc,
			relationships, assets, "collection1", arf_report_mapping);

	struct oscap_htable_iterator *hit = oscap_htable_iterator_new(arf_report_mapping);
	while (oscap_htable_iterator_has_more(hit)) {
		const struct oscap_htable_item *report_mapping_item = oscap_htable_iterator_next(hit);
		const char *oval_filename = report_mapping_item->key;
		const char *report_id = report_mapping_item->value;
		const char *report_file = oscap_htable_get(oval_result_mapping, oval_filename);
		struct oscap_source *oval_source = oscap_htable_get(oval_result_sources, report_file);
		xmlDoc *oval_result_doc = oscap_source_get_xmlDoc(oval_source);

		ds_rds_create_report(doc, reports, oval_result_doc, report_id);
	}
	oscap_htable_iterator_free(hit);

	xmlAddChild(root, reports);

	*ret = doc;
	return 0;
}

int ds_rds_create_from_dom(xmlDocPtr *ret, xmlDocPtr sds_doc,
		xmlDocPtr tailoring_doc, const char *tailoring_filepath,
		char *tailoring_doc_timestamp, xmlDocPtr xccdf_result_file_doc,
		struct oscap_htable *oval_result_sources,
		struct oscap_htable *oval_result_mapping,
		struct oscap_htable *arf_report_mapping)
{
	return _ds_rds_create_from_dom(ret, sds_doc, tailoring_doc,
			tailoring_filepath, tailoring_doc_timestamp,
			xccdf_result_file_doc, oval_result_sources, oval_result_mapping,
			arf_report_mapping, false);
}

static int ds_rds_create_from_dom_clone(xmlDocPtr *ret, xmlDocPtr sds_doc,
		xmlDocPtr tailoring_doc, const char *tailoring_filepath,
		char *tailoring_doc_timestamp, xmlDocPtr xccdf_result_file_doc,
		struct oscap_htable *oval_result_sources,
		struct oscap_htable *oval_result_mapping,
		struct oscap_htable *arf_report_mapping)
{
	return _ds_rds_create_from_dom(ret, sds_doc, tailoring_doc,
			tailoring_filepath, tailoring_doc_timestamp,
			xccdf_result_file_doc, oval_result_sources, oval_result_mapping,
			arf_report_mapping, true);
}

struct oscap_source *ds_rds_create_source(struct oscap_source *sds_source, struct oscap_source *tailoring_source, struct oscap_source *xccdf_result_source, struct oscap_htable *oval_result_sources, struct oscap_htable *oval_result_mapping, struct oscap_htable *arf_report_mapping, const char *target_file)
{
	xmlDoc *sds_doc = oscap_source_get_xmlDoc(sds_source);
	if (sds_doc == NULL) {
		return NULL;
	}

	xmlDoc *result_file_doc = oscap_source_get_xmlDoc(xccdf_result_source);
	if (result_file_doc == NULL) {
		return NULL;
	}

	xmlDoc *tailoring_doc = NULL;
	char *tailoring_doc_timestamp = NULL;
	const char *tailoring_filepath = NULL;
	if (tailoring_source) {
		tailoring_doc = oscap_source_get_xmlDoc(tailoring_source);
		if (tailoring_doc == NULL) {
			return NULL;
		}
		tailoring_filepath = oscap_source_get_filepath(tailoring_source);
		struct stat file_stat;
		if (stat(tailoring_filepath, &file_stat) == 0) {
			const size_t max_timestamp_len = 32;
			tailoring_doc_timestamp = malloc(max_timestamp_len);
			strftime(tailoring_doc_timestamp, max_timestamp_len, "%Y-%m-%dT%H:%M:%S", localtime(&file_stat.st_mtime));
		}
	}

	xmlDocPtr rds_doc = NULL;

	if (ds_rds_create_from_dom_clone(&rds_doc, sds_doc, tailoring_doc, tailoring_filepath, tailoring_doc_timestamp, result_file_doc,
			oval_result_sources, oval_result_mapping, arf_report_mapping) != 0) {
		free(tailoring_doc_timestamp);
		return NULL;
	}
	free(tailoring_doc_timestamp);
	return oscap_source_new_from_xmlDoc(rds_doc, target_file);
}

int ds_rds_create(const char* sds_file, const char* xccdf_result_file, const char** oval_result_files, const char* target_file)
{
	struct oscap_source *sds_source = oscap_source_new_from_file(sds_file);
	struct oscap_source *xccdf_result_source = oscap_source_new_from_file(xccdf_result_file);
	struct oscap_htable *oval_result_sources = oscap_htable_new();
	struct oscap_htable *oval_result_mapping = oscap_htable_new();
	struct oscap_htable *arf_report_mapping = oscap_htable_new();

	int result = 0;
	// this check is there to allow passing NULL instead of having to allocate
	// an empty array
	if (oval_result_files != NULL)
	{
		while (*oval_result_files != NULL)
		{
			struct oscap_source *oval_source = oscap_source_new_from_file(*oval_result_files);
			if (oscap_source_get_xmlDoc(oval_source) == NULL) {
				result = -1;
				oscap_source_free(oval_source);
			} else {
				if (!oscap_htable_add(oval_result_sources, *oval_result_files, oval_source)) {
					result = -1;
					oscap_source_free(oval_source);
				}
			}
			oval_result_files++;
		}
	}
	if (result == 0) {
		struct oscap_source *target_rds = ds_rds_create_source(sds_source, NULL, xccdf_result_source, oval_result_sources, oval_result_mapping, arf_report_mapping, target_file);
		result = target_rds == NULL;
		if (result == 0) {
			result = oscap_source_save_as(target_rds, NULL);
		}
		oscap_source_free(target_rds);
	}
	oscap_htable_free(oval_result_sources, (oscap_destruct_func) oscap_source_free);
	oscap_htable_free(oval_result_mapping, (oscap_destruct_func) free);
	oscap_htable_free(arf_report_mapping, (oscap_destruct_func) free);
	oscap_source_free(sds_source);
	oscap_source_free(xccdf_result_source);

	return result;
}
