<?php
/**
 * Returns the title for a provided pdf_form_type.
 */
function pdf_forms_type_title($type) {
  $title = db_query('SELECT label FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $type))->fetchField();
  return $title;
}

/**
 * Returns the title for a provided submission.
 */
function pdf_forms_submission_title($sid) {
  $submission = pdf_forms_load($sid);
	$title = pdf_forms_type_title($submission->bundle);
	return $title;
}

/**
 * Helper function to detect if a machine name exsits.
 */
function pdf_forms_type_exists($machine_value) {
  return (db_query('SELECT COUNT(*) FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $machine_value))->fetchField() > 0);
}

/**
 * PDF Form Type Creation.
 */
function pdf_forms_type_create() {
  $pdf_form_type = new stdClass();
  $pdf_form_type->bundle = '';
  $pdf_form_type->label = '';
  $pdf_form_type->description = '';
  $pdf_form_type->fid = 0;
  $pdf_form_type->status = 0;

  return drupal_get_form('pdf_forms_type_edit', $pdf_form_type);
}

/**
 * PDF Form Type edit form.
 */
function pdf_forms_type_edit($form, &$form_state, $pdf_form_type = NULL) {
  if (!is_object($pdf_form_type)) {
    $results = db_query('SELECT * FROM {pdf_forms_type} WHERE bundle = :bundle', array(':bundle' => $pdf_form_type));
    foreach ($results as $row) {
      $pdf_form_type = $row;
      break;
    }
  }

  $form['pdf_form_type'] = array(
    '#type' => 'value',
    '#value' => $pdf_form_type,
  );

  if ($pdf_form_type->bundle == '') {
    $form['bundle'] = array(
      '#type' => 'machine_name',
      '#title' => t('Machine Name'),
      '#description' => t('The machine-readable name must contain only lowercase letters, numbers, and underscores.'),
      '#required' => TRUE,
      '#default_value' => $pdf_form_type->bundle,
      '#machine_name' => array(
        'exists' => 'pdf_forms_type_exists',
      ),
    );
  }

  $form['label'] = array(
    '#type' => 'textfield',
    '#title' => t('Title'),
    '#description' => t('Enter the title users will see when submitting this form.'),
    '#required' => TRUE,
    '#default_value' => $pdf_form_type->label,
  );

  $form['description'] = array(
    '#type' => 'textarea',
    '#title' => t('Description'),
    '#description' => t('This will be displayed on the administrative pages only.'),
    '#default_value' => $pdf_form_type->description,
  );

  $form['status'] = array(
    '#type' => 'checkbox',
    '#title' => t('Active'),
    '#default_value' => $pdf_form_type->status,
  );

  $form['file'] = array(
    '#type' => 'fieldset',
    '#title' => t('PDF Form'),
    '#prefix' => '<div id="pdf-upload-wrapper">',
    '#suffix' => '</div>',
  );

  $form['file']['fname_wrapper']['filename'] = array(
    '#type' => 'textfield',
    '#title' => t('Filename'),
    '#description' => t('Enter the name for the generated PDF file, you can use tokens in the file name. Do not include File extension.'),
    '#default_value' => (empty($pdf_form_type->filename) ? '' : $pdf_form_type->filename),
  );

  // @TODO: Have a checkbox which allows user to "enable" certain entity types which will then be added to $token_types
  $token_types = array('pdf_form', 'user');
  $form['file']['fname_wrapper']['tokens'] = array(
    '#theme' => 'token_tree',
    '#token_types' => $token_types,
		'#dialog' => TRUE,
  );

  $form['file']['fid'] = array(
    '#type' => 'managed_file',
    '#title' => t('File'),
    '#description' => t('Upload the PDF form to be filled out by submissions.'),
    '#required' => TRUE,
    '#upload_location' => variable_get('pdf_forms_template_path', 'public://pdf_form_templates'),
    '#field_name' => 'fid',
    '#default_value' => $pdf_form_type->fid,
  );
	if($pdf_form_type->fid > 0){
		$form['file']['fid']['#disabled'] = TRUE;
	}

  $form['file']['fid']['#ajax']['wrapper'] = 'pdf-upload-wrapper';

  // Display if FID is not empty or we just uploaded a file.
  if ($pdf_form_type->fid > 0 || isset($form_state['values']['fid'])) {
    $form['fields'] = array(
      '#type' => 'fieldset',
      '#title' => t('Field Mapping'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
      '#tree' => TRUE,
    );

    // Load up the file, then pass to our pdftk functions to extract fields.
    $file = ($pdf_form_type->fid > 0) ? file_load($pdf_form_type->fid) : file_load($form_state['values']['fid']);

    module_load_include('inc', 'pdf_forms', 'includes/pdf_forms.pdftk');
    if ($fields = pdf_forms_fields($file)) {
			// Options for textfield type
			$type_options = array('Text' => 'Text', 'Arithmetic' => 'Arithmetic');

			// Options for reformating
			//@TODO - Format options should only be available for field type "text"
			$format_options = array('Raw' => 'Raw','English Notation' => 'English Notation');

      $form['fields']['table'] = array(
        '#tree' => TRUE,
        '#theme' => 'pdf_forms_mapping_table',
        '#header' => array(
          array('data' => t('Name')),
					array('data' => t('Type')),
          array('data' => t('Format')),
					array('data' => t('Prefix')),
					array('data' => t('Value')),
					array('data' => t('Suffix')),
					array('data' => t('Description')),
					array('data' => t('Weight')),
					array('data' => t('Page num')),
        ),

        '#rows' => array(),
      );

      // Load up any fields from the database that have been saved and update the default values with the stored values.
      $field_map_data = array();
      if (!empty($pdf_form_type->bundle)) {
        $query = db_select('pdf_forms_field_mapping', 'm');
        $query->fields('m', array())
          ->condition('m.bundle', $pdf_form_type->bundle, '=');
        $results = $query->execute();

        // Update the form's default values.
        foreach ($results as $row) {

					$row->fieldinfo = unserialize($row->fieldinfo);
          $field_map_data[$row->machine_name] = array(
						'type' => $row->type,
            'format' => $row->format,
            'signers' => $row->signers,
            'prefix' => $row->prefix,
            'suffix' => $row->suffix,
            'value' => $row->value,
						'weight' => $row->weight,
						'pagenum' => $row->pdf_page,
						'description' => isset($row->fieldinfo['tooltip']) ? $row->fieldinfo['tooltip'] : '',
          );
        }
      }

      foreach ($fields as $index => $field) {

        if (!isset($field['fieldinfo'])) {
          $field['fieldinfo'] = NULL;
        }
				$finfo = unserialize($field['fieldinfo']);

        $id = str_replace(' ', '^', $field['name']);

        $id = trim($id);

        $field_name = array(
          '#id' => 'fields-table-' . $id . '-name',
          '#type' => 'markup',
          '#markup' => $field['name'],
        );
				// Detect field type
				$ftype = isset($field_map_data[$id]) ? $field_map_data[$id]['type'] : $field['type'];
        $fformat = isset($field_map_data[$id]) ? $field_map_data[$id]['format'] : $field['format'];

				if(in_array($ftype, $type_options)){
					$field_type = array(
						'#id' => 'fields-table-' . $id . '-type',
						'#type' => 'select',
						'#options' => $type_options,
						'#default_value' => $ftype,
						'#required' => TRUE,
					);

					$field_format = array(
					  '#id' => 'fields-table-' . $id . '-format',
					  '#type' => 'select',
					  '#options' => $format_options,
					  '#default_value' => $fformat,
					  '#required' => TRUE,
					);
				}
				else{
					$field_type = array(
						'#id' => 'fields-table-' . $id . '-type',
						'#type' => 'textfield',
						'#default_value' => $ftype,
						'#size' => '6',
						'#attributes' => array('readonly' => 'readonly'),
					);

					$field_format = array(
					  '#id' => 'fields-table-' . $id . '-format',
					  '#type' => 'textfield',
					  '#default_value' => $format_options['Raw'],
					  '#size' => '6',
					  '#attributes' => array('readonly' => 'readonly'),
					);
				}

        $prefix = array(
          '#id' => 'fields-table-' . $id . '-prefix',
          '#type' => 'textarea',
          '#default_value' => isset($field_map_data[$id]) ? $field_map_data[$id]['prefix'] : '',
        );

        $value = array(
          '#id' => 'fields-table-' . $id . '-value',
          '#type' => 'textarea',
          '#default_value' => isset($field_map_data[$id]) ? $field_map_data[$id]['value'] : '',
        );

        $suffix = array(
          '#id' => 'fields-table-' . $id . '-suffix',
          '#type' => 'textarea',
          '#default_value' => isset($field_map_data[$id]) ? $field_map_data[$id]['suffix'] : '',
        );

				if(isset($field_map_data[$id])){
					$tooltip = strlen($field_map_data[$id]['description']) ? $field_map_data[$id]['description'] : $finfo['tooltip'];
				}
				else{
					$tooltip = $finfo['tooltip'];
				}
        $fielddesc = array(
          '#id' => 'fields-table-' . $id . '-fielddesc',
          '#type' => 'textarea',
          '#default_value' => $tooltip,
        );

        $fieldweight = array(
          '#id' => 'fields-table-' . $id . '-fieldweight',
          '#type' => 'textfield',
					'#size' => 3,
					'#default_value' => isset($field_map_data[$id]) ? $field_map_data[$id]['weight'] : 0,
					'#attributes' => array('class' => array('fweight', 'item-row-weight')),
        );

        $fieldpage = array(
          '#id' => 'fields-table-' . $id . '-fieldpage',
          '#type' => 'textfield',
					'#size' => 3,
					'#default_value' => isset($field_map_data[$id]) ? $field_map_data[$id]['pagenum'] : 1,
        );

        $machine_name = array(
          '#id' => 'fields-table-' . $id . '-machine-name',
          '#type' => 'value',
          '#default_value' => $field['name'],
        );
        $fieldinfo = array(
          '#id' => 'fields-table-' . $id . '-fieldinfo',
          '#type' => 'hidden',
          '#default_value' => $field['fieldinfo'],
        );

        $form['fields']['table'][$id] = array(
          'name' => &$field_name,
          'type' => &$field_type,
          'format' => &$field_format,
          'prefix' => &$prefix,
          'value' => &$value,
          'suffix' => &$suffix,
					'fielddesc' => &$fielddesc,
					'fieldweight' => &$fieldweight,
					'pagenum' => &$fieldpage,
          'machine_name' => &$machine_name,
          'fieldinfo' => &$fieldinfo,
        );

        $form['fields']['table']['#rows'][$id] = array(
          'name' => &$field_name,
          'type' => &$field_type,
          'format' => &$field_format,
          'prefix' => &$prefix,
          'value' => &$value,
          'suffix' => &$suffix,
					'fielddesc' => &$fielddesc,
					'fieldweight' => &$fieldweight,
					'pagenum' => &$fieldpage,
          'fieldinfo' => &$fieldinfo,
        );

        unset($field_name);
        unset($field_type);
        unset($field_format);
        unset($prefix);
        unset($value);
        unset($suffix);
				unset($fielddesc);
				unset($fieldweight);
				unset($fieldpage);
        unset($machine_name);
        unset($fieldinfo);
      }
      $token_types = array('pdf_form');
      $form['fields']['token_tree'] = array(
        '#theme' => 'token_tree',
        '#token_types' => $token_types,
        '#dialog' => TRUE,
      );

      // Include the Token information.
    }
    else {
      // Notify user no fields were found in the form.
      $form['fields']['table'] = array(
        '#type' => 'markup',
        '#markup' => t('No PDF Form fields were found in this PDF.'),
      );
    }

  }

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );

  return $form;
}

/**
 * Submit handler for the PDF Form Type form.
 */
function pdf_forms_type_edit_submit($form, &$form_state) {

  $values = $form_state['values'];
  $pdf_form_type = $values['pdf_form_type'];

  $new_bundle = FALSE;
  $pdf_form_type->bundle = isset($values['bundle']) ? $values['bundle'] : $pdf_form_type->bundle;
  $pdf_form_type->label = $values['label'];
  $pdf_form_type->status = $values['status'];
  $pdf_form_type->description = $values['description'];
  $pdf_form_type->filename = (strlen($values['filename'])) ? $values['filename'] : $pdf_form_type->bundle;
  $primary_keys = array();

  // Let's deal with the file.
  if (empty($pdf_form_type->fid) || $pdf_form_type->fid !== $values['fid']) {
    // The file has been changed.
    if (empty($pdf_form_type->fid)) {
      // This is the first time this bundle has been saved.
      $new_bundle = TRUE;
    }
    /*elseif ($pdf_form_type->fid !== $values['fid']) {
      // We are editing a bundle - the File ID has changed, which means we need to remove the old file + regenerate mappings
      // we need to delete mappings and update the fid.
      $old_file = file_load($pdf_form_type->fid);
      if ($old_file) {
        file_usage_delete($old_file, 'pdf_forms', $pdf_form_type->bundle, 0);
        file_delete($old_file);
				db_delete('pdf_forms_field_mapping')
					->condition('bundle', $pdf_form_type->bundle)
					->execute();
				unset($form_state['input']['fields']);
      }
    }*/
    $file = file_load($values['fid']);
    $file->status = FILE_STATUS_PERMANENT;
    file_save($file);
    file_usage_add($file, 'pdf_forms', $pdf_form_type->bundle, 0);
    $pdf_form_type->fid = $values['fid'];
  }
  // Save the bundle.
  $primary_keys = isset($values['bundle']) ? array() : array('bundle');
  drupal_write_record('pdf_forms_type', $pdf_form_type, $primary_keys);

  /* Field mappings */
  $new_record = db_query('SELECT COUNT(*) FROM {pdf_forms_field_mapping} WHERE bundle = :bundle', array(':bundle' => $pdf_form_type->bundle))->fetchField();
  $data = array();

	if ($new_bundle == TRUE) {
		$fields = $form_state['values']['fields']['table'];
	}
	else{
		$fields = $form_state['input']['fields']['table'];
	}

	foreach ($fields as $machine_name => $field) {
		$field['fieldinfo'] = unserialize($field['fieldinfo']);
		$field['fieldinfo']['tooltip'] = $field['fielddesc'];
		$field['fieldinfo'] = serialize($field['fieldinfo']);
		$data = array(
			'bundle' => $pdf_form_type->bundle,
			'machine_name' => $machine_name,
			'name' => $machine_name,
			'prefix' => $field['prefix'],
			'value' => $field['value'],
			'suffix' => $field['suffix'],
			'type' => $field['type'],
		  'format' => $field['format'],
			'weight' => (int)$field['fieldweight'],
			'pdf_page' => $field['pagenum'],
			'fieldinfo' => $field['fieldinfo'],
		);

		// Check if this is a new record.
		$primary_keys = ($new_record > 0) ? array('bundle', 'machine_name') : array();
		$result = drupal_write_record('pdf_forms_field_mapping', $data, $primary_keys);
	}


  if ($new_bundle == TRUE) {
    menu_rebuild();
    drupal_set_message(t("New PDF Form Created"));
    $form_state['redirect'] = "admin/content/pdf_forms/{$pdf_form_type->bundle}/edit";
  }
  else {
    drupal_set_message(t("PDF Form updates saved"));
  }
}

/**
 * PDF Form Type delete function.
 */
function pdf_forms_type_delete($type) {
  $submissions = db_select('pdf_forms_data', 'd')
    ->fields('d', array('submission_id'))
    ->condition('d.bundle', $type)
    ->execute()
    ->fetchAll();

  $entities = array();
  foreach ($submissions as $submission) {
    $entities[] = $submission->submission_id;
  }
  if (count($entities)) {
    $entities = pdf_forms_load_multiple($entities);
    entity_get_controller('pdf_form')->deleteMultiple($entities);
  }
  db_delete('pdf_forms_type')
    ->condition('bundle', $type)
    ->execute();

  db_delete('pdf_forms_field_mapping')
    ->condition('bundle', $type)
    ->execute();

  drupal_goto('admin/content/pdf_forms');
}

/**
 * Implements the uri callback.
 */
function pdf_forms_uri($entity) {
  return array(
    'path' => "pdf_forms_submission/{$entity->submission_id}",
  );
}

/**
 * Implements view callback.
 */
function pdf_forms_view($entity, $view_mode = 'full') {
	$entity = pdf_forms_load($entity);
  $entity->content = array(
    '#view_mode' => $view_mode,
  );
  // field_attach_prepare_view($entity_type, array($entity->submission_id => $entity), $view_mode);
  // entity_prepare_view($entity_type, array($entity->submission_id => $entity));
  // $entity->content += field_attach_view($entity_type, $entity, $view_mode);.
  if (empty($entity->fid)) {
    module_load_include('inc', 'pdf_forms', 'includes/pdf_forms.pdftk');
    $file = pdf_forms_pdftk_create_completed_form($entity);
    $entity->fid = $file->fid;
    pdf_forms_save($entity);
  }
  $file = file_load($entity->fid);
  $entity->content['pdf'] = array(
    '#theme' => 'pdf_formatter_default',
    '#file' => $file,
    '#width' => '100%',
    '#height' => '500px',
  );

  // We are purposely not showing field api fields here.
  if (module_exists('rules')) {
    rules_invoke_event('pdf_forms_view', $entity, $file);
  }
  return $entity->content;
}

/**
 * Callback for creating a new Form Submission.
 */
function pdf_forms_create($bundle) {
  module_load_include('inc', 'pdf_forms', 'includes/pdf_forms.pdftk');
  // $entity = entity_get_controller('pdf_form')->create($bundle);
  return drupal_get_form('pdf_forms_edit', $bundle);
}

/**
 * Form for editing/creating a form submission.
 */
function pdf_forms_edit($form, &$form_state, $bundle = NULL) {
  $form_state['bundle'] = $bundle;
  $map = array();
  $results = db_query('SELECT machine_name, type, value, pdf_page, fieldinfo FROM {pdf_forms_field_mapping} WHERE bundle = :bundle ORDER BY weight ASC', array(':bundle' => $bundle));
  foreach ($results as $field_map) {
    $map[$field_map->pdf_page][$field_map->machine_name]['type'] = $field_map->type;
		$map[$field_map->pdf_page][$field_map->machine_name]['value'] = $field_map->value;
    $map[$field_map->pdf_page][$field_map->machine_name]['info'] = unserialize($field_map->fieldinfo);
  }

  if (count($map)) {
		foreach($map as $fieldset => $field){
			$form['fieldset'.$fieldset] = array(
				'#type' => 'fieldset',
				'#title' => t($fieldset),
				'#collapsible' => TRUE,
				'#collapsed' => isset($collapsed) ? TRUE : FALSE,
			);
			foreach ($field as $fname => $fvalue) {
				$formfield = array();
				if ($field[$fname]['type'] == "Button") {
					$formfield['#type'] = "checkbox";
				}
				elseif ($field[$fname]['type'] == "Radio") {
					$formfield['#type'] = "radios";
					$options = $field[$fname]['info']['options'];
					$vars = array();
					foreach ($options as $option) {
						$vars[$option] = $option;
					}
					$formfield['#options'] = $vars;
					if(in_array("Off", $options)) $formfield['#default_value'] = 'Off';
				}
				elseif ($field[$fname]['type'] == "Choice") {
					$formfield['#type'] = "select";
					$options = $field[$fname]['info']['options'];
					$vars = array();
					foreach ($options as $option) {
						$vars[$option] = $option;
					}
					$formfield['#options'] = $vars;
				}
				else {
					$formfield['#type'] = "textfield";
					$formfield['#maxlength'] = "256";
					$formfield['#default_value'] = $field[$fname]['value'];
				}
				// Check required field
				if(isset($field[$fname]['info']['required'])){
					$formfield['#required'] = TRUE;
				}
				// Check readonly field
				if(isset($field[$fname]['info']['readonly'])){
					$formfield['#disabled'] = $field[$fname]['info']['readonly'];
					$formfield['#type'] = "hidden";
					$hiddenfield = TRUE;
				}
				// Add field description
				if(isset($field[$fname]['info']['tooltip'])){
					$formfield['#description'] = $field[$fname]['info']['tooltip'];
				}
				//replace underscores with spaces in field names. Also make first letter capital
				$preprocessed_fname = ucfirst(str_replace('^', ' ', $fname));

				$formfield['#title'] = $preprocessed_fname;
				if(isset($hiddenfield)){
					$form['fieldset'.$fieldset]['hiddenfields'][$fname] = $formfield;
					unset($hiddenfield);
				}
				else{
					$form['fieldset'.$fieldset]['fields'][$fname] = $formfield;
				}
			}
			if(!isset($form['fieldset'.$fieldset]['fields'])){
				hide($form['fieldset'.$fieldset]);
			}
			$collapsed = TRUE;
		}
  }
  else {
    $form['nofields'] = array(
      '#markup' => '<p>No fields found for this submission</p>',
    );
  }

  $form['submit_button'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
    '#weight' => 500,
  );

  return $form;
}

/**
 *
 */
function pdf_forms_edit_submit($form, &$form_state) {

  unset($form_state['values']['form_id'], $form_state['values']['form_token'], $form_state['values']['form_build_id'], $form_state['values']['op'], $form_state['values']['submit_button'], $form_state['values']['delete_button']);

  $submission = entity_get_controller('pdf_form')->create(array('bundle'=>$form_state['bundle']));
  global $user;
  $submission->uid = $user->uid;
  $data = pdf_forms_pdftk_prepare_datatypes($submission->bundle, $form_state['values']);
  $file = pdf_forms_pdftk_generate_completed_form($submission, $data);
  $values = serialize($form_state['values']);
  $submission->pdfdata = $values;
  $submission->fid = $file->fid;
  $save_result = pdf_forms_save($submission);
  //@TODO - check if saved properly, only display message if it is
  drupal_set_message(t('Submission !submission for !bundle saved', array('!submission' => $save_result->submission_id, '!bundle' => $save_result->bundle)));
  $form_state['redirect'] = "pdf_form/submissions/{$submission->submission_id}/edit";
}

/*
 * Callback function for submission revisions list
 */
function pdf_forms_submission_revisions($submission_id, $type = 'pdf_form'){
	$info = entity_get_info($type);
	$submission = pdf_forms_load($submission_id);
	dpm($submission);
  $revisions = db_select($info['revision table'], 'r')
    ->fields('r', array('vid', 'uid', 'changed'))
    ->condition($info['entity keys']['id'], $submission->submission_id)
    ->execute()
    ->fetchAllAssoc($info['entity keys']['revision']);
	dpm($revisions);
	$header = array(
		t('Changed'),
		t('User'),
		array('data' => t('Operations'), 'colspan' => 2),
	);
	$rows = array();
	foreach($revisions as $key => $revision){
    $row = array();
    $operations = array();
		$user = user_load($revision->uid);
		$row[] = l(format_date($revision->changed, 'short'), 'pdf_form/submissions/'.$submission_id.'/revisions/'.$revision->vid.'/view');
		$row[] = l($user->name, 'user/'.$user->uid);
		$operations[] = l(t('revert'), 'pdf_form/submissions/'.$submission_id.'/revisions/'.$revision->vid.'/revert');
		$operations[] = l(t('delete'), 'pdf_form/submissions/'.$submission_id.'/revisions/'.$revision->vid.'/delete');

		$rows[] = array_merge($row, $operations);
	}

  $build['node_revisions_table'] = array(
    '#theme' => 'table',
    '#rows' => $rows,
    '#header' => $header,
  );
	return $build;
}

function pdf_forms_submission_revisions_view($sid, $rid){

}
/**
 * Save by calling our controller.
 */
function pdf_forms_save($entity) {
  return entity_get_controller('pdf_form')->save($entity);
}
