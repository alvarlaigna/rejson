#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <dirent.h>
#include "minunit.h"
#include "../src/json_object.h"

#define _JSTR(e) "\"" #e "\""

MU_TEST(test_jo_create_literal_null) {
    Node *n;
    const char *json = "null";

    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL == n);
}

MU_TEST(test_jo_create_literal_true) {
    Node *n;
    const char *json = "true";

    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(N_BOOLEAN == n->type);
    mu_check(n->value.boolval);
    Node_Free(n);
}

MU_TEST(test_jo_create_literal_false) {
    Node *n;
    const char *json = "false";

    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(N_BOOLEAN == n->type);
    mu_check(!n->value.boolval);
    Node_Free(n);
}

MU_TEST(test_jo_create_literal_integer) {
    Node *n;
    const char *json;

    json = "0";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_INTEGER == n->type);
    mu_assert_int_eq(0, n->value.intval);
    Node_Free(n);

    json = "-0";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_INTEGER == n->type);
    mu_assert_int_eq(0, n->value.intval);
    Node_Free(n);

    json = "6379";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_INTEGER == n->type);
    mu_assert_int_eq(6379, n->value.intval);
    Node_Free(n);

    json = "-42";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_INTEGER == n->type);
    mu_assert_int_eq(-42, n->value.intval);
    Node_Free(n);
}

MU_TEST(test_jo_create_literal_double) {
    Node *n;
    const char *json;

    json = "0.0";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_NUMBER == n->type);
    mu_assert_double_eq(0, n->value.numval);
    Node_Free(n);

    json = "-0.0";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_NUMBER == n->type);
    mu_assert_double_eq(0, n->value.numval);
    Node_Free(n);

    json = "63.79";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_NUMBER == n->type);
    mu_assert_double_eq(63.79, n->value.numval);
    Node_Free(n);

    json = "-4.2";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_NUMBER == n->type);
    mu_assert_double_eq(-4.2, n->value.numval);
    Node_Free(n);

    // TODO: check more notations
}

MU_TEST(test_jo_create_literal_string) {
    Node *n;
    char err[4096];
    const char *json = "\"foo\"";

    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_STRING == n->type);
    mu_check(0 == strncmp("foo", n->value.strval.data, n->value.strval.len));
    Node_Free(n);

    // TODO: more weird chars
}

MU_TEST(test_jo_create_literal_dict) {
    Node *n;
    const char *json = "{}";

    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_DICT == n->type);
    mu_assert_int_eq(0, n->value.dictval.len);
    Node_Free(n);

    json = "{" _JSTR(foo) ": " _JSTR(bar) "}";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_DICT == n->type);
    mu_assert_int_eq(1, n->value.dictval.len);
    Node_Free(n);

    json = "{"
                _JSTR(foo) ": " _JSTR(bar) ", "
                _JSTR(baz) ": " "42"
            "}";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_DICT == n->type);
    mu_assert_int_eq(2, n->value.dictval.len);
    Node_Free(n);
}

MU_TEST(test_jo_create_literal_array) {
    Node *n;
    const char *json = "[]";

    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_ARRAY == n->type);
    mu_assert_int_eq(0, n->value.arrval.len);
    Node_Free(n);

    json = "[" _JSTR(foo) ", " _JSTR(bar) ", 42]";
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(json, strlen(json), &n, NULL));
    mu_check(NULL != n);
    mu_check(N_ARRAY == n->type);
    mu_assert_int_eq(3, n->value.dictval.len);
    Node_Free(n);
}

MU_TEST(test_jo_create_object) {
    const char SampleJSON[] =
        "{"
            _JSTR(foo) ": {"
                _JSTR(bar) ": ["
                    _JSTR(element0) ","
                    _JSTR(element1)
                    "],"
               _JSTR(inner object) ": {" \
                   _JSTR(baz) ":" _JSTR(qux)
               "}"
           "}"
        "}";

    Node *n1, *n2, *n3, *n4;
    mu_check(JSONOBJECT_OK == CreateNodeFromJSON(SampleJSON, strlen(SampleJSON), &n1, NULL));
    mu_check(n1);
    mu_check(N_DICT == n1->type);
    mu_check(1 == n1->value.dictval.len);

    mu_check(OBJ_ERR == Node_DictGet(n1, "f00", &n2));
    mu_check(OBJ_ERR == Node_DictGet(n1, "bar", &n2));
    mu_check(OBJ_ERR == Node_DictGet(n1, "baz", &n2));
    mu_check(OBJ_OK == Node_DictGet(n1, "foo", &n2));
    mu_check(N_DICT == n2->type);
    mu_assert_int_eq(2, n2->value.dictval.len);

    mu_check(OBJ_OK == Node_DictGet(n2, "bar", &n3));
    mu_check(N_ARRAY == n3->type);
    mu_assert_int_eq(2, n3->value.arrval.len);

    mu_check(OBJ_OK == Node_ArrayItem(n3, 0, &n4));
    mu_check(N_STRING == n4->type);
    mu_check(0 == strncmp("element0", n4->value.strval.data, n4->value.strval.len));

    mu_check(OBJ_OK == Node_ArrayItem(n3, 1, &n4));
    mu_check(N_STRING == n4->type);
    mu_check(0 == strncmp("element1", n4->value.strval.data, n4->value.strval.len));

    mu_check(OBJ_OK == Node_DictGet(n2, "inner object", &n3));
    mu_check(N_DICT == n3->type);
    mu_assert_int_eq(1, n3->value.dictval.len);

    mu_check(OBJ_OK == Node_DictGet(n3, "baz", &n4));
    mu_check(N_STRING == n4->type);
    mu_check(0 == strncmp("qux", n4->value.strval.data, n4->value.strval.len));

    Node_Free(n1);
}

MU_TEST(test_oj_null) {
    Node *n;
    char *str = NULL;

    n = NULL;
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp("null", str, strlen(str)));
    free(str);
}

MU_TEST(test_oj_boolean) {
    Node *n;
    char *str = NULL;

    n = NewBoolNode(0);
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp("false", str, strlen(str)));
    free(str);
    Node_Free(n);

    n = NewBoolNode(1);
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp("true", str, strlen(str)));
    free(str);
    Node_Free(n);
}

MU_TEST(test_oj_integer) {
    Node *n;
    char *str = NULL;

    n = NewIntNode(0);
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp("0", str, strlen(str)));
    free(str);
    Node_Free(n);

    n = NewIntNode(42);
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp("42", str, strlen(str)));
    free(str);
    Node_Free(n);

    n = NewIntNode(-6379);
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp("-6379", str, strlen(str)));
    free(str);
    Node_Free(n);
}

MU_TEST(test_oj_string) {
    Node *n;
    char *str = NULL;

    n = NewCStringNode("foo");
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp(_JSTR(foo), str, strlen(str)));
    free(str);
    Node_Free(n);
}

MU_TEST(test_oj_keyval) {
    Node *n;
    char *str = NULL;
    char *json = _JSTR(foo) ":" _JSTR(bar);

    n = NewKeyValNode("foo", 3, NewCStringNode("bar"));
    mu_check(n);
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp(json, str, strlen(str)));
    free(str);
    Node_Free(n);
}

MU_TEST(test_oj_dict) {
    Node *n;
    char *str = NULL;
    char *json = "{" _JSTR(foo) ":" _JSTR(bar) "}";

    n = NewDictNode(1);
    mu_check(n);
    mu_check(OBJ_OK == Node_DictSet(n, "foo", NewCStringNode("bar")));
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp(json, str, strlen(str)));
    free(str);
    Node_Free(n);
}

MU_TEST(test_oj_array) {
    Node *n;
    char *str = NULL;
    char *json = "[" _JSTR(foo) ",42]";

    n = NewArrayNode(2);
    mu_check(n);
    mu_check(OBJ_OK == Node_ArrayAppend(n, NewCStringNode("foo")));
    mu_check(OBJ_OK == Node_ArrayAppend(n, NewIntNode(42)));
    mu_check(JSONOBJECT_OK == SerializeNodeToJSON(n, NULL, NULL, NULL, &str));
    mu_check(str);
    mu_check(0 == strncmp(json, str, strlen(str)));
    free(str);
    Node_Free(n);
}

MU_TEST_SUITE(test_json_literals) {
    MU_RUN_TEST(test_jo_create_literal_null);
    MU_RUN_TEST(test_jo_create_literal_true);
    MU_RUN_TEST(test_jo_create_literal_false);
    MU_RUN_TEST(test_jo_create_literal_integer);
    MU_RUN_TEST(test_jo_create_literal_double);
    MU_RUN_TEST(test_jo_create_literal_string);
    MU_RUN_TEST(test_jo_create_literal_dict);
    MU_RUN_TEST(test_jo_create_literal_array);
}

MU_TEST_SUITE(test_json_object) {
    MU_RUN_TEST(test_jo_create_object);
}

MU_TEST_SUITE(test_object_to_json) {
    MU_RUN_TEST(test_oj_null);
    MU_RUN_TEST(test_oj_boolean);
    MU_RUN_TEST(test_oj_integer);
    MU_RUN_TEST(test_oj_string);
    MU_RUN_TEST(test_oj_keyval);
    MU_RUN_TEST(test_oj_dict);
    MU_RUN_TEST(test_oj_array);
}

int main(int argc, char *argv[]) {
    MU_RUN_SUITE(test_json_literals);
    MU_RUN_SUITE(test_json_object);
    MU_RUN_SUITE(test_object_to_json);
    MU_REPORT();
    return minunit_fail;
}