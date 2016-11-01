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
			else{
				$fields[$findex]['fieldinfo']['required'] = 0;
			}
			// Check if readonly field
			if($field['FieldFlags'][0] == "1" || $field['FieldFlags'][0] == "3"){
				$fields[$findex]['fieldinfo']['readonly'] = TRUE;
			}
			else{
				$fields[$findex]['fieldinfo']['readonly'] = 0;
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
 * Returns the field mapping for an entity bundle.
 */
function pdf_forms_pdftk_get_field_map($entity) {
  $field_mappings = db_query('SELECT * FROM {pdf_forms_field_mapping} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchAll(PDO::FETCH_ASSOC);
  return ($field_mappings);
}

/**
 * Prepare data array for PDF population.
 * @param $field_mappings - array of the field mappings
 * @param $token_objects - objects which are used with token_replace
 * @param $values - any submitted form values
 * @return an Array of values to insert into the form
 */
function pdf_forms_pdftk_process_fields($field_mappings, $token_objects = array(), $values = array()) {

  foreach ($field_mappings as $field_map) {
    // If form value is provided override field mappings
    $values[$field_map['machine_name']] = (!empty($values[$field_map['machine_name']]) ? $values[$field_map['machine_name']] : $field_map['value']);

    //Replace Tokens
    $values[$field_map['machine_name']] = (!empty($values[$field_map['machine_name']]) ? token_replace($values[$field_map['machine_name']], $token_objects) : $values[$field_map['machine_name']]);
    $field_map['suffix'] = (!empty($field_map['suffix']) ? token_replace($field_map['suffix'], $token_objects) : "");
    $field_map['prefix'] = (!empty($field_map['prefix']) ? token_replace($field_map['prefix'], $token_objects) : "");
  	$field_map['fieldinfo'] = unserialize($field_map['fieldinfo']);

    //Process field types
    switch ($field_map['type']) {

      case "Arithmetic":
        $m = new ctools_math_expr;
        $m->suppress_errors = TRUE;
        $values[$field_map['machine_name']] = $m->evaluate($values[$field_map['machine_name']]);
        break;

      case "Button":
       if (!empty($values[$field_map['machine_name']])) {
         $values[$field_map['machine_name']] = $field_map['fieldinfo']['options'][$values[$field_map['machine_name']]];
          /*
          // Find option not equal to Off
          foreach($field_map['fieldinfo']['options'] as $option) {
            if($option != "Off"){
              break;
            }*/
          }
        break;
    }

    //Process field formatting
    switch ($field_map['format']) {
      case "English Notation":
        $values[$field_map['machine_name']] = floatval(filter_var($values[$field_map['machine_name']],FILTER_SANITIZE_NUMBER_FLOAT));
        setlocale(LC_MONETARY, 'en_US');
        $values[$field_map['machine_name']] = money_format('%!i', $values[$field_map['machine_name']]);
        break;
    }

    //Attach the prefix and suffix
    $values[$field_map['machine_name']] = $field_map['prefix'] . $values[$field_map['machine_name']] . $field_map['suffix'];
  }

  return $values;
}

/**
 * Generates a filled out PDF form provided with the entity data.
 *
 * @param $entity - This is the pdf_form entity.
 * @param $token_objects - This is a list of entities we would like to use Token values from
 * @param $values - if we have values already from a form submission
 * @param $template - are we specifing the PDF file to use?
 * @return the File object.
 */
function pdf_forms_pdftk_create_completed_form($entity, $values = array(), $token_objects = array(), $template = FALSE) {

  $save_path = variable_get('pdf_forms_submissions_path', 'public://');
  // Load up the template.
  if (!$template) {
    $template_fid = db_query('SELECT fid FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchField();
    $template = file_load($template_fid);
  }

  $account = user_load($entity->uid);
  $token_objects['user'] = $account;
  $token_objects['pdf_form'] = $entity;

  // Load up the field map data.
  $map = array();
  $map = pdf_forms_pdftk_get_field_map($entity);

	if(count($map)){
		$map = pdf_forms_pdftk_process_fields($map, $token_objects, $values);

		// Create the xfdf
		module_load_include('inc', 'pdf_forms', 'includes/pdf_forms.xfdf');

		$filename_raw = db_query('SELECT filename FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $entity->bundle))->fetchField();
		$filename = token_replace($filename_raw, $token_objects);
    //Sanitize Filename
    $filename = preg_replace(array('/\s/', '/\.[\.]+/', '/[^\w_\.\-]/'), array('_', '.', ''), $filename);

		$destination =  "{$save_path}/{$filename}";
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
