<?php
/**
 * @file
 * Utility functions for use with the Islandora Simple Workflow module.
 */

/**
 * Query for inactive objects.
 *
 * @return array
 *   An array containing the results of the inactive objects query.
 */
function islandora_simple_workflow_get_inactive_objects() {
  $connection = islandora_get_tuque_connection();
  $query = <<<EOQ
SELECT ?object ?title ?owner FROM <#ri> WHERE
{
  ?object <fedora-model:label> ?title ;
          <fedora-model:state> <info:fedora/fedora-system:def/model#Inactive> ;
          <fedora-model:ownerId> ?owner
  !optionals
  !filters
}
ORDER BY ?object
EOQ;
  $optionals = (array) module_invoke('islandora_xacml_api', 'islandora_basic_collection_get_query_optionals', 'view');
  $filter_modules = array(
    'islandora_xacml_api',
    'islandora',
  );
  $filters = array();
  foreach ($filter_modules as $module) {
    $filters = array_merge_recursive($filters, (array) module_invoke($module, 'islandora_basic_collection_get_query_filters', 'view'));
  }
  $filter_map = function ($filter) {
    return "FILTER($filter)";
  };
  $sparql_query = format_string($query, array(
    '!optionals' => !empty($optionals) ? ('OPTIONAL {{' . implode('} UNION {', $optionals) . '}}') : '',
    '!filters' => !empty($filters) ? implode(' ', array_map($filter_map, $filters)) : '',
  ));
  $results = $connection->repository->ri->sparqlQuery($sparql_query);
  return $results;
}

function is_islandora_simple_workflow_object_accepted($pid) {
    $db_and = db_and();
    $db_and->condition('w.state', 'a');
    $db_and->condition('w.pid', $pid);
    $query = db_select('trace_workflow_pids', 'w')
        ->fields('w', array('pid'))
        ->condition($db_and)
        ->execute();

    $num = $query->rowCount();
    if ($num > 0) {
        return TRUE;
    } 
    return FALSE;

}
function does_islandora_simple_workflow_namespace_need_accept($namespace) {
    $db_and = db_and();
    $db_and->condition('w.needs_approval', '1');
    $db_and->condition('w.namespace', $namespace);
    $query = db_select('islandora_workflow_namespace_role', 'w')
        ->fields('w', array('needs_approval'))
        ->condition($db_and)
        ->execute();

    $num = $query->rowCount();
    if ($num > 0) {
        return TRUE;
    } 
    return FALSE;

}
function can_islandora_simple_workflow_object_read_user_role($pid) {
    global $user;
    $user_roles_array = array_keys($user->roles);
    $namespace = islandora_get_namespace($pid);
    $query = db_select('islandora_workflow_namespace_role', 'iwnr');
    $db_or = db_or();
    $db_or->condition('iwnr.namespace', $namespace);
    $db_or->condition('iwnr.namespace', 'ALL');
    $db_and = db_and();
    $db_and->condition('iwnr.rid', $user_roles_array, 'IN');
    $db_and->condition($db_or);
    $query->condition($db_and);
    $query->addExpression('COUNT(iwnr.namespace)', 'namespace_count');
    $query->groupBy('iwnr.namespace');
    $results = $query->execute();
    $num = $results->rowCount();
    if ($num > 0) {
        $namespace_count_record = $results->fetchObject();
        if ($namespace_count_record && ($namespace_count_record->namespace_count > 0)) {
            return TRUE;
        }
    }
    return FALSE;
}

function islandora_simple_workflow_retrieve_namespace_published_policy($namespace) {

    // get the policy from the database that corresponds to the namespace

    $query = db_select('trace_workflow_namespace_policies', 't')
        ->fields('t', array('published_policy'))
        ->condition('t.namespace', $namespace)
        ->range(0, 1)
        ->execute();

    $num = $query->rowCount();
    if ($num > 0) {
        $result = $query->fetchObject();
        return $result->published_policy;
    } else {
        watchdog('islandora_simple_workflow', 'islandora_simple_workflow_retrieve_namespace_published_policy does not have policies for  %p.', array('%p' => $namespace), WATCHDOG_ERROR);
        return FALSE;
    }
}


/**
 * Rules action callback; set a POLiCY datastream on an object and return.
 */
function islandora_simple_workflow_object_policy_update(AbstractObject $object) {
    // determine the namespace of the object
    $pid = $object->id;
    $namespace = islandora_get_namespace($pid);


    // get the policy from the database that corresponds to the namespace
    $policy_xml = islandora_simple_workflow_retrieve_namespace_published_policy($namespace);
    // add the policy to the object
    
    $pid = $object->id;
    $repository = $object->repository;

    return $repository->api->m->modifyDatastream($pid, 'POLICY', array('dsString' => $policy_xml));

}
