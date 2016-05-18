<?php

/**
 * Validate that PDFTK is installed.
 *
 * @param $pdftk_path
 *   The string path to the PDFTK executable.
 *
 * @return Boolean whether or not we could successfully execute PDFTK.
 */
function pdf_forms_pdftk_check($pdftk_path = 'pdftk') {
  if ($pdftk_path == '') {
    // Means PDFTK is in the system path.
    $pdftk_path = 'pdftk';
  }
  $data = array();
  $status = NULL;
  exec($pdftk_path, $output, $status);
  if (in_array($status, array(127, 126))) {
    // Could not execute PDFTK.
    return FALSE;
  }
  else {
    // PDFTK returned a good response.
    return TRUE;
  }
}

/**
 * Scans the PDF Form fields from the provided file.
 *
 * @param $file
 *   The file object passed to use to scan for fields.
 *
 * @return An Array of fields that were detected in the PDF file, returns false if no file provided or no fields available.
 */
function pdf_forms_fields($file = NULL) {
  $fields = array();
  $pdftk = variable_get('pdf_forms_pdftk_path', 'pdftk');
  if ($file == NULL) {
    return FALSE;
  }

  // Call PDFTK with exec and scan the resulting information.
  $data = array();
  $status = NULL;
  $pdf_file = drupal_realpath($file->uri);
  $command = $pdftk . ' ' . escapeshellarg($pdf_file) . ' dump_data_fields';
  exec($command, $data, $status);

  if (count($data) == 0) {
    watchdog('pdf_forms', 'Could not locate form fields in PDF: %file.', array('%file' => $pdf_file), WATCHDOG_WARNING);
  }
  else {
    // Parse the returned data.
    $field_map = array(
      'FieldType' => 'type',
      'FieldName' => 'name',
      'FieldFlags' => 'flags',
      'FieldJustification' => 'justification',
      'FieldStateOption' => 'fieldinfo',
			'FieldNameAlt' => 'FieldNameAlt', 
    );
    $fieldsdata = array();
    $index = -1;

    foreach ($data as $id => $row) {
      if ($row == '---') {
        // Next Field.
        $index++;
        continue;
      }
      $output = explode(':', $row);

      $column = $output[0];
      $value = $output[1];
      if (in_array($column, array_keys($field_map))) {
        // Set the column's data value.
        $fieldsdata[$index][$column][] = trim($value);
      }
    }
    $fields = array();

    foreach ($fieldsdata as $findex => $field) {
			$fields[$findex]['fieldinfo'] = array();
      if ($field['FieldType'][0] == "Button" && ($field['FieldFlags'][0] == "49152" || $field['FieldFlags'][0] == "49154")) {
        $field['FieldType'][0] = "Radio";			
      }
			// Check if required field
			if($field['FieldFlags'][0] == "49154" || $field['FieldFlags'][0] == "2" || $field['FieldFlags'][0] == "3"){
				$fields[$findex]['fieldinfo']['required'] = TRUE;
			}
			// Check if readonly field
			if($field['FieldFlags'][0] == "1" || $field['FieldFlags'][0] == "3"){
				$fields[$findex]['fieldinfo']['readonly'] = TRUE;
			}			
			
			if(isset($field['FieldStateOption'])){
				$fields[$findex]['fieldinfo']['options'] = $field['FieldStateOption'];
				// Delete "Off" value from options if required field
				if((($key = array_search("Off", $fields[$findex]['fieldinfo']['options'])) !== false) && isset($fields[$findex]['fieldinfo']['required'])) {
					unset($fields[$findex]['fieldinfo']['options'][$key]);
				}						
				unset($field['FieldStateOption']);
			}
			if(isset($field['FieldNameAlt'])){
				$fields[$findex]['fieldinfo']['tooltip'] = $field['FieldNameAlt'][0];
				unset($field['FieldNameAlt']);
			}			
			else{
				$fields[$findex]['fieldinfo']['tooltip'] = '';
			}
      foreach ($field as $ftype => $fvalue) {
        $fvalue = $fvalue[0];        
        $fields[$findex][$field_map[$ftype]] = $fvalue;
      }
			$fields[$findex]['fieldinfo'] = serialize($fields[$findex]['fieldinfo']);
    }
  }
  if (!count($fields)) {
    return FALSE;
  }

  return $fields;
}

/**
 * Generate data from data entity for submission.
 */
function pdf_forms_pdftk_prepare_submission_data($entity, $data_entities = NULL) {
  // Load up any entities that are being passed.
  if (!is_null($data_entities)) {
    foreach ($data_entities as $data_entity) {
      $token_objects[$data_entity->type()] = $data_entity->value();
    }
  }

  // Load up the field map data.
  $map = array();
  $map = pdf_forms_pdftk_get_field_map($entity, $token_objects);

  return $map;
}

/**
 * Prepare data arrray for PDF population.
 */
function pdf_forms_pdftk_prepare_datatypes($bundle, $values) {
  $results = db_query('SELECT machine_name, type, fieldinfo FROM {pdf_forms_field_mapping} WHERE bundle = :bundle', array(':bundle' => $bundle));
  foreach ($results as $field_map) {
    $map[$field_map->machine_name]['type'] = $field_map->type;
		$map[$field_map->machine_name]['fieldinfo'] = unserialize($field_map->fieldinfo);
  }
  foreach ($values as $name => $value) {
    if ($map[$name]['type'] == "Button" && ($value)) {
			// Find option not equal to Off
			foreach($map[$name]['fieldinfo']['options'] as $option){
				if($option != "Off"){
					break; 
				}
			}
      $values[$name] = $option;
    }
  }

  return $values;
}

/**
 * Generates a filled out PDF form provided with the entity data.
 *
 * @param $entity - This is the pdf_form entity.
 * @param $token_objects - This is a list of entities we would like to use Token values from
 *
 * @return the File object.
 */
function pdf_forms_pdftk_create_completed_form($entity, $data_entities = NULL, $template = FALSE) {

  $save_path = variable_get('pdf_form_save_path', 'public://');
  // Load up the template.
  if (!$template) {
    $template_fid = db_query('SELECT fid FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchField();
    $template = file_load($template_fid);
  }

  $token_objects = array();

  $account = user_load($entity->uid);
  $token_objects['user'] = $account;
  $token_objects['pdf_form'] = $entity;

  // Load up any entities that are being passed.
  if (!is_null($data_entities)) {
    foreach ($data_entities as $data_entity) {
      $token_objects[$data_entity->type()] = $data_entity->value();
    }
  }

  // Load up the field map data.
  $map = array();
  $map = pdf_forms_pdftk_get_field_map($entity, $token_objects);
	if(count($map)){
		$map = pdf_forms_pdftk_prepare_datatypes($entity->bundle, $map);

		// Create the xfdf
		module_load_include('inc', 'pdf_forms', 'includes/pdf_forms.xfdf');

		$filename_raw = db_query('SELECT filename FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchField();
		$filename = token_replace($filename_raw, $token_objects);
		
		$file_schema = file_default_scheme();
		$destination =  "{$file_schema}://{$filename}";

		$XFDF = pdf_forms_generate_xfdf("{$filename}.pdf.xfdf", $map, 'UTF-8');

		// Store data to a file.
		$xfdffile = file_save_data($XFDF, "{$destination}.pdf.xfdf", FILE_EXISTS_REPLACE);
		
		file_usage_add($xfdffile, 'pdf_forms', $entity->bundle, $entity->submission_id);

		$pdftk = variable_get('pdf_forms_pdftk_path', 'pdftk');

		// Start shell command

		$command = $pdftk . ' '
     			. escapeshellarg(drupal_realpath($template->uri))
     			. ' fill_form '
     			. escapeshellarg(drupal_realpath($xfdffile->uri))
     			. ' output - flatten drop_xfa';

		ob_start();
		passthru($command);
		$response = ob_get_clean();
		
		$file = file_save_data($response, "{$destination}.pdf", FILE_EXISTS_RENAME);
		file_usage_add($file, 'pdf_forms', $entity->bundle, $entity->submission_id);
		$file->pdfdata=$map; 
		module_invoke_all('pdf_forms_generate', $file);
	}
	else{
		$file = file_load($template_fid);
		$file->pdfdata = array();
	}
  return $file;
}

/**
 *
 */
function pdf_forms_pdftk_generate_completed_form($entity, $data_entities = NULL, $template = FALSE) {

  $save_path = variable_get('pdf_form_save_path', 'public://');
  // Load up the template.
  if (!$template) {
    $template_fid = db_query('SELECT fid FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchField();
    $template = file_load($template_fid);
  }

  $token_objects = array();

  $account = user_load($entity->uid);
  $token_objects['user'] = $account;
  $token_objects['pdf_form'] = $entity;

  // Load up the field map data.
  $map = array();
  $map = pdf_forms_pdftk_get_field_map($entity, $token_objects);
  // Create the xfdf.
  module_load_include('inc', 'pdf_forms', 'includes/pdf_forms.xfdf');

  $filename_raw = db_query('SELECT filename FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchField();
  $filename = token_replace($filename_raw, $token_objects);

  $file_schema = file_default_scheme();
  $destination = "{$file_schema}://{$filename}";
dpm($data_entities);
  $XFDF = pdf_forms_generate_xfdf("{$filename}.pdf.xfdf", $data_entities, 'UTF-8');
dpm($XFDF);
  // Store data to a file.
  $xfdffile = file_save_data($XFDF, "{$destination}.pdf.xfdf", FILE_EXISTS_REPLACE);
dpm($xfdffile);
  file_usage_add($xfdffile, 'pdf_forms', $entity->bundle, $entity->submission_id);

  $pdftk = variable_get('pdf_forms_pdftk_path', 'pdftk');

  // Start shell command.
  $command = $pdftk . ' '
    . escapeshellarg(drupal_realpath($template->uri))
    . ' fill_form '
    . escapeshellarg(drupal_realpath($xfdffile->uri))
    . ' output - flatten drop_xfa';

  ob_start();
  passthru($command);
  $response = ob_get_clean();
dpm($command);	
dpm($response);
  $file = file_save_data($response, "{$destination}.pdf", FILE_EXISTS_REPLACE);
  file_usage_add($file, 'pdf_forms', $entity->bundle, $entity->submission_id);
  $file->pdfdata = $map;
  module_invoke_all('pdf_forms_generate', $file);

  return $file;
}

/**
 * Returns the field mapping for an entity bundle.
 */
function pdf_forms_pdftk_get_field_map($entity, $token_objects) {

  $map = array();
  $results = db_query('SELECT * FROM {pdf_forms_field_mapping} WHERE bundle = :bundle', array(':bundle' => $entity->bundle));

  foreach ($results as $field_map) {
    // For some reason token_replace doesn't like being passed an array of entities (doing so results in it ignoring relation tokens)
    // So instead we iterate through each entity type, running token_replace for each entity seperatly.
    foreach ($token_objects as $entity_type => $entity_object) {
      $token_object_array = array($entity_type => $entity_object);
      if (!empty($field_map->prefix)) {
        $field_map->prefix = token_replace($field_map->prefix, $token_object_array);
      }
      if (!empty($field_map->value)) {
        $field_map->value = token_replace($field_map->value, $token_object_array);
      }
      if (!empty($field_map->suffix)) {
        $field_map->suffix = token_replace($field_map->suffix, $token_object_array);
      }
    }

    $map[$field_map->machine_name] = $field_map->prefix . $field_map->value . $field_map->suffix;
  }

  return $map;
}
